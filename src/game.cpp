#include "game.h"

#include "config.h"

#include <windows.h>
#include <MinHook.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <atomic>
#include <deque>
#include <optional>
#include <regex>
#include <unordered_set>
#include <set>
#include <string>
#include <vector>

namespace {

using SetupAndRenderFn = void(__fastcall*)(void* screenView, void* uiRenderContext);
SetupAndRenderFn g_originalSetupAndRender = nullptr;

std::mutex g_stateMutex;
GameState g_state;
ULONGLONG g_lastValidTick = 0;

std::optional<uintptr_t> FindPattern(const std::string& pattern) {
    std::vector<int> bytes; // -1 = wildcard
    for (size_t i = 0; i < pattern.size();) {
        if (pattern[i] == ' ') {
            ++i;
        } else if (pattern[i] == '?') {
            bytes.push_back(-1);
            while (i < pattern.size() && pattern[i] == '?') ++i;
        } else {
            bytes.push_back(static_cast<int>(std::strtoul(pattern.substr(i, 2).c_str(), nullptr, 16)));
            i += 2;
        }
    }
    if (bytes.empty()) return std::nullopt;

    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint8_t* start = base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        if (size < bytes.size()) continue;
        for (size_t i = 0; i <= size - bytes.size(); ++i) {
            size_t j = 0;
            while (j < bytes.size() && (bytes[j] == -1 || start[i + j] == bytes[j])) ++j;
            if (j == bytes.size()) return reinterpret_cast<uintptr_t>(start + i);
        }
    }
    return std::nullopt;
}

template <typename T>
T Read(uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

// Copies an MSVC std::string (data/SSO buffer, size at +0x10, capacity at +0x18).
void ReadString(uintptr_t address, char* out, size_t outSize) {
    out[0] = 0;
    const auto size = Read<size_t>(address + 0x10);
    const auto capacity = Read<size_t>(address + 0x18);
    if (size > 4096 || capacity < size) return;
    const char* data = capacity >= 16 ? Read<const char*>(address) : reinterpret_cast<const char*>(address);
    const size_t n = size < outSize - 1 ? size : outSize - 1;
    std::memcpy(out, data, n);
    out[n] = 0;
}

uintptr_t CallVirtual(uintptr_t object, uintptr_t index) {
    using Fn = uintptr_t(__fastcall*)(uintptr_t);
    return reinterpret_cast<Fn>(Read<uintptr_t*>(object)[index])(object);
}

bool Finite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool ReadCameraUnsafe(uintptr_t clientInstance, GameState& out) {
    const Offsets& c = g_offsets;
    const auto minecraftGame = Read<uintptr_t>(clientInstance + c.clientInstanceMinecraftGame);
    if (!minecraftGame) return false;
    const auto gameRenderer = Read<uintptr_t>(minecraftGame + c.minecraftGameGameRenderer);
    if (!gameRenderer) return false;

    const uintptr_t levelRenderer = c.clientInstanceLevelRenderer
                                        ? Read<uintptr_t>(clientInstance + c.clientInstanceLevelRenderer)
                                        : CallVirtual(clientInstance, c.clientInstanceGetLevelRenderer);
    if (!levelRenderer) return false; // not in a world
    const auto rendererPlayer = Read<uintptr_t>(levelRenderer + c.levelRendererPlayer);
    if (!rendererPlayer) return false;

    out.origin = Read<Vec3>(rendererPlayer + c.levelRendererPlayerCameraPos);
    std::memcpy(out.view, reinterpret_cast<void*>(gameRenderer + c.gameRendererViewMatrix), sizeof(out.view));
    std::memcpy(out.proj, reinterpret_cast<void*>(gameRenderer + c.gameRendererProjMatrix), sizeof(out.proj));

    for (float f : out.view)
        if (!std::isfinite(f)) return false;
    for (float f : out.proj)
        if (!std::isfinite(f)) return false;
    return Finite(out.origin);
}

bool ReadPlayerUnsafe(uintptr_t clientInstance, GameState& out) {
    const Offsets& c = g_offsets;
    if (!c.clientInstanceGetLocalPlayer) return false;
    const uintptr_t player = CallVirtual(clientInstance, c.clientInstanceGetLocalPlayer);
    if (!player) return false;

    if (c.actorDimension) {
        // shared_ptr<Dimension>: the object pointer comes first
        const auto dimension = Read<uintptr_t>(player + c.actorDimension);
        if (dimension) ReadString(dimension + c.dimensionName, out.dimension, sizeof(out.dimension));
    }
    if (!c.actorStateVector) return false;
    const auto stateVector = Read<uintptr_t>(player + c.actorStateVector);
    if (!stateVector) return false;
    out.playerPos = Read<Vec3>(stateVector);
    return Finite(out.playerPos);
}

bool ReadWorldUnsafe(uintptr_t clientInstance, GameState& out) {
    const Offsets& c = g_offsets;
    if (!c.clientInstanceMinecraft) return false;
    const auto minecraft = Read<uintptr_t>(clientInstance + c.clientInstanceMinecraft);
    if (!minecraft) return false;
    const auto session = Read<uintptr_t>(minecraft + c.minecraftGameSession);
    if (!session || Read<uint8_t>(session + c.gameSessionHasLevel) != 1) return false;
    const auto levelState = Read<uint8_t*>(session + c.gameSessionLevelState);
    if (!levelState || *levelState != 1) return false;
    const auto level = Read<uintptr_t>(session + c.gameSessionLevel);
    if (!level) return false;
    const auto levelData = Read<uintptr_t>(level + c.levelLevelData);
    if (!levelData) return false;
    ReadString(levelData + c.levelDataName, out.world, sizeof(out.world));
    return out.world[0] != 0;
}

bool ReadScreenNameUnsafe(uintptr_t screenView, char* out, size_t outSize) {
    const Offsets& c = g_offsets;
    out[0] = 0;
    if (!c.screenViewVisualTree) return false;
    const auto tree = Read<uintptr_t>(screenView + c.screenViewVisualTree);
    if (!tree) return false;
    const auto root = Read<uintptr_t>(tree + c.visualTreeRoot);
    if (!root) return false;
    ReadString(root + c.uiControlName, out, outSize);
    return out[0] != 0;
}

// Wrong offsets after a game update must not crash the game, so every read is guarded.
#ifdef _MSC_VER
#define GUARDED(expr)                        \
    __try {                                  \
        return expr;                         \
    } __except (EXCEPTION_EXECUTE_HANDLER) { \
        return false;                        \
    }
#else
#define GUARDED(expr) return expr;
#endif

bool ReadClientInstance(uintptr_t uiContext, uintptr_t& out) {
    GUARDED((out = Read<uintptr_t>(uiContext + g_offsets.uiContextClientInstance)) != 0)
}
bool ReadCamera(uintptr_t clientInstance, GameState& out) {
    GUARDED(ReadCameraUnsafe(clientInstance, out))
}
bool ReadPlayer(uintptr_t clientInstance, GameState& out) {
    GUARDED(ReadPlayerUnsafe(clientInstance, out))
}
bool ReadWorld(uintptr_t clientInstance, GameState& out) {
    GUARDED(ReadWorldUnsafe(clientInstance, out))
}
bool ReadScreenName(uintptr_t screenView, char* out, size_t outSize) {
    GUARDED(ReadScreenNameUnsafe(screenView, out, outSize))
}

std::mutex g_screenMutex;
std::string g_currentScreen;
ULONGLONG g_lastScreenTick = 0;

std::mutex g_deathMutex;
bool g_deathPending = false;
DeathInfo g_death;
ULONGLONG g_lastDeathScreenTick = 0;
std::set<std::string> g_seenScreens;

void CheckScreen(uintptr_t screenView) {
    char name[64];
    if (!ReadScreenName(screenView, name, sizeof(name))) return;

    {
        std::lock_guard lock(g_screenMutex);
        g_currentScreen = name;
        g_lastScreenTick = GetTickCount64();
    }

    // Log each screen name once, to help with fixing things after an update.
    if (g_seenScreens.size() < 100 && g_seenScreens.insert(name).second) Log(std::string("Screen: ") + name);

    if (!std::strstr(name, "death")) return;
    const ULONGLONG now = GetTickCount64();
    const bool newDeath = now - g_lastDeathScreenTick > 3000;
    g_lastDeathScreenTick = now;
    if (!newDeath) return;

    GameState last;
    {
        std::lock_guard lock(g_stateMutex);
        last = g_state;
    }
    if (!last.hasPlayer) return;

    std::lock_guard lock(g_deathMutex);
    g_death.feetPos = {last.playerPos.x, last.playerPos.y - kEyeHeight, last.playerPos.z};
    std::memcpy(g_death.dimension, last.dimension, sizeof(g_death.dimension));
    std::memcpy(g_death.world, last.world, sizeof(g_death.world));
    g_deathPending = true;
    Log("Death screen seen, saving death waypoint");
}

// ---------------------------------------------------------------- zoom / fullbright

std::atomic<float> g_zoom{1.0f};
std::atomic<float> g_gamma{-1.0f};
bool g_zoomHooked = false;
bool g_gammaHooked = false;

using RenderLevelFn = void(__fastcall*)(void*, void*, void*);
RenderLevelFn g_originalRenderLevel = nullptr;
using GetGammaFn = float(__fastcall*)(void*, void*);
GetGammaFn g_originalGetGamma = nullptr;

// Display Gamma Ramp Fallback (using Windows GDI)
HWND GetGameHwnd() {
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return fg;

    HWND found = nullptr;
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        DWORD p = 0;
        GetWindowThreadProcessId(hwnd, &p);
        if (p == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
            *reinterpret_cast<HWND*>(lParam) = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found ? found : fg;
}

std::mutex g_rampMutex;
WORD g_originalRamp[3][256]{};
bool g_hasOriginalRamp = false;
bool g_rampActive = false;

bool ApplyZoomUnsafe(uintptr_t levelRenderer, float zoom) {
    const Offsets& c = g_offsets;
    const auto player = Read<uintptr_t>(levelRenderer + c.levelRendererPlayer);
    if (!player) return false;
    *reinterpret_cast<float*>(player + c.levelRendererPlayerFovX) *= zoom;
    *reinterpret_cast<float*>(player + c.levelRendererPlayerFovY) *= zoom;
    return true;
}

bool ApplyZoom(uintptr_t levelRenderer, float zoom) {
    GUARDED(ApplyZoomUnsafe(levelRenderer, zoom))
}

void __fastcall RenderLevelDetour(void* levelRenderer, void* screenContext, void* unk) {
    g_originalRenderLevel(levelRenderer, screenContext, unk);
    // Same as Latite: the FOV is scaled after rendering and picked up by the next frame.
    const float zoom = g_zoom.load();
    if (zoom > 1.001f && levelRenderer) ApplyZoom(reinterpret_cast<uintptr_t>(levelRenderer), zoom);
}

float __fastcall GetGammaDetour(void* options, void* unk) {
    const float original = g_originalGetGamma(options, unk);
    const float gamma = g_gamma.load();
    return gamma >= 0 ? gamma : original;
}

bool InstallGammaHook(const Offsets& candidate) {
    // 1. Try candidate profile signature if configured
    if (!candidate.gammaSig.empty() &&
        InstallHook(candidate.gammaSig, false, reinterpret_cast<void*>(&GetGammaDetour),
                    reinterpret_cast<void**>(&g_originalGetGamma), "getGamma (profile)")) {
        return true;
    }

    // 2. Known fallback signatures across Bedrock versions
    static const char* kSignatures[] = {
        // Bedrock 1.26.5x (option 0x32 / 50)
        "48 83 EC 38 48 8B 05 ? ? ? ? 48 31 E0 48 89 44 24 ? 48 8B 01 48 8B 40 08 48 8D 54 24 ? 41 B8 32 00 00 00",
        // Wildcarded option 0x32
        "48 83 EC ? ? ? ? ? ? ? ? ? ? ? 48 89 44 24 ? 48 8B 01 48 8B 40 ? 48 8D 54 24 ? 41 B8 32 00 00 00",
        // Horion / 1.20 - 1.26 (option 0x10D / 269)
        "48 83 EC ? 80 B9 ? ? ? ? ? 48 8D 54 24 ? 48 8B 01 74 ? 41 B8 0D 01 00 00",
        // Wildcarded option 0x10D
        "48 83 EC ? ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B 01 ? ? 41 B8 0D 01 00 00",
        // Common variant
        "48 83 EC ? ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B 01 48 8B 40 ? 48 8D 54 24 ? 41 B8 0D 01 00 00"
    };

    for (const char* sig : kSignatures) {
        if (InstallHook(sig, false, reinterpret_cast<void*>(&GetGammaDetour),
                        reinterpret_cast<void**>(&g_originalGetGamma), "getGamma (fallback sig)")) {
            return true;
        }
    }

    // 3. Dynamic instruction search: find option opcode and scan back for function prologue
    static const char* kOpcodes[] = {
        "41 B8 0D 01 00 00",
        "41 B8 32 00 00 00"
    };
    for (const char* op : kOpcodes) {
        auto match = FindPattern(op);
        if (match) {
            uintptr_t addr = *match;
            auto* p = reinterpret_cast<const uint8_t*>(addr);
            for (int back = 1; back < 128; ++back) {
                if (p[-back] == 0x48 && p[-back + 1] == 0x83 && p[-back + 2] == 0xEC) {
                    uintptr_t fn = addr - back;
                    if (MH_CreateHook(reinterpret_cast<void*>(fn), reinterpret_cast<void*>(&GetGammaDetour),
                                      reinterpret_cast<void**>(&g_originalGetGamma)) == MH_OK &&
                        MH_EnableHook(reinterpret_cast<void*>(fn)) == MH_OK) {
                        Log("getGamma: hooked via dynamic opcode search");
                        return true;
                    }
                }
            }
        }
    }

    Log("getGamma: memory hook not available for this version; display gamma ramp will be used as fallback");
    return false;
}

// ---------------------------------------------------------------- chat

constexpr size_t kMaxChat = 64;
char g_chatText[kMaxChat][256];

size_t ReadChatUnsafe(uintptr_t clientInstance) {
    const Offsets& c = g_offsets;
    if (!c.clientInstanceGuiData || !c.guiMessageSize) return 0;
    const auto guiData = Read<uintptr_t>(clientInstance + c.clientInstanceGuiData);
    if (!guiData) return 0;
    const auto begin = Read<uintptr_t>(guiData + c.guiDataMessages);
    const auto end = Read<uintptr_t>(guiData + c.guiDataMessages + 8);
    if (!begin || end < begin) return 0;
    size_t count = (end - begin) / c.guiMessageSize;
    if (count > kMaxChat) count = kMaxChat;
    for (size_t i = 0; i < count; ++i)
        ReadString(begin + i * c.guiMessageSize + c.guiMessageText, g_chatText[i], sizeof(g_chatText[i]));
    return count;
}

size_t ReadChat(uintptr_t clientInstance) {
#ifdef _MSC_VER
    __try {
        return ReadChatUnsafe(clientInstance);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
#else
    return ReadChatUnsafe(clientInstance);
#endif
}

std::mutex g_chatMutex;
std::deque<ChatCoords> g_chatCoords;
std::unordered_set<size_t> g_seenChat;
bool g_chatPrimed = false;

// Finds "x y z" (also "x, y, z" or "x/y/z") in a chat line.
bool ParseCoords(const char* text, Vec3& out) {
    static const std::regex pattern(R"((-?\d{1,8})[\s,/]+(-?\d{1,3})[\s,/]+(-?\d{1,8}))");
    std::cmatch m;
    if (!std::regex_search(text, m, pattern)) return false;
    const int y = std::stoi(m[2].str());
    if (y < -64 || y > 320) return false;
    out = {std::stof(m[1].str()), static_cast<float>(y), std::stof(m[3].str())};
    return true;
}

void CheckChat(uintptr_t clientInstance) {
    const size_t count = ReadChat(clientInstance);
    for (size_t i = 0; i < count; ++i) {
        // Strip Minecraft colour codes (section sign + one character).
        std::string text;
        for (const char* p = g_chatText[i]; *p; ++p) {
            if (static_cast<unsigned char>(p[0]) == 0xC2 && static_cast<unsigned char>(p[1]) == 0xA7 && p[2]) {
                p += 2;
                continue;
            }
            text += *p;
        }
        if (!g_seenChat.insert(std::hash<std::string>{}(text)).second || !g_chatPrimed) continue;

        ChatCoords found;
        if (!ParseCoords(text.c_str(), found.pos)) continue;
        found.text = text.substr(0, 120);
        std::lock_guard lock(g_chatMutex);
        g_chatCoords.push_back(found);
        if (g_chatCoords.size() > 10) g_chatCoords.pop_front();
    }
    g_chatPrimed = true; // messages that were already there when we injected are ignored
}

bool InstallHook(const std::string& sig, bool isCall, void* detour, void** original, const char* name) {
    if (sig.empty()) return false;
    auto address = FindPattern(sig);
    if (!address) {
        Log(std::string(name) + ": signature not found");
        return false;
    }
    if (isCall) *address += 5 + *reinterpret_cast<int32_t*>(*address + 1);
    if (MH_CreateHook(reinterpret_cast<void*>(*address), detour, original) != MH_OK ||
        MH_EnableHook(reinterpret_cast<void*>(*address)) != MH_OK) {
        Log(std::string(name) + ": hook failed");
        return false;
    }
    Log(std::string(name) + ": hooked");
    return true;
}

void __fastcall SetupAndRenderDetour(void* screenView, void* uiRenderContext) {
    g_originalSetupAndRender(screenView, uiRenderContext);

    uintptr_t clientInstance = 0;
    if (!uiRenderContext || !ReadClientInstance(reinterpret_cast<uintptr_t>(uiRenderContext), clientInstance))
        return;

    if (screenView) CheckScreen(reinterpret_cast<uintptr_t>(screenView));

    GameState state;
    state.valid = ReadCamera(clientInstance, state);
    if (!state.valid) return;
    state.hasPlayer = ReadPlayer(clientInstance, state);
    ReadWorld(clientInstance, state);
    CheckChat(clientInstance);

    // setupAndRender runs once per screen layer; keep the last good snapshot.
    std::lock_guard lock(g_stateMutex);
    g_state = state;
    g_lastValidTick = GetTickCount64();
}

} // namespace

namespace game {

std::string GameVersion() {
    static const std::string version = [] {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        DWORD handle = 0;
        const DWORD size = GetFileVersionInfoSizeW(path, &handle);
        if (!size) return std::string("unknown");
        std::vector<uint8_t> data(size);
        VS_FIXEDFILEINFO* info = nullptr;
        UINT len = 0;
        if (!GetFileVersionInfoW(path, 0, size, data.data()) ||
            !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &len) || !info)
            return std::string("unknown");
        char text[64];
        snprintf(text, sizeof(text), "%u.%u.%u.%u", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                 HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
        return std::string(text);
    }();
    return version;
}

bool Init(std::string& error) {
    Log("Minecraft version: " + GameVersion());

    for (const Offsets& profile : BuiltInProfiles()) {
        if (g_config.profile != "auto" && g_config.profile != profile.name) continue;

        Offsets candidate = profile;
        ApplyOverrides(candidate);
        auto address = FindPattern(candidate.setupAndRenderSig);
        if (!address) {
            Log("Profile " + candidate.name + ": signature not found");
            continue;
        }
        if (candidate.sigIsCall) {
            // E8 <rel32>: target = next instruction + rel32
            *address += 5 + *reinterpret_cast<int32_t*>(*address + 1);
        }

        g_offsets = candidate;
        if (MH_CreateHook(reinterpret_cast<void*>(*address), reinterpret_cast<void*>(&SetupAndRenderDetour),
                          reinterpret_cast<void**>(&g_originalSetupAndRender)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void*>(*address)) != MH_OK) {
            error = "Failed to hook setupAndRender.";
            return false;
        }
        Log("Using profile " + candidate.name);
        g_zoomHooked = InstallHook(candidate.renderLevelSig, candidate.renderLevelSigIsCall,
                                   reinterpret_cast<void*>(&RenderLevelDetour),
                                   reinterpret_cast<void**>(&g_originalRenderLevel), "renderLevel");
        g_gammaHooked = InstallGammaHook(candidate);
        return true;
    }

    error = "Minecraft " + GameVersion() + " is not supported yet (signature not found).";
    return false;
}

void Cleanup() {
    RestoreDisplayGammaRamp();
}

GameState State() {
    std::lock_guard lock(g_stateMutex);
    GameState state = g_state;
    // No fresh snapshot means we left the world (main menu, loading screen).
    if (GetTickCount64() - g_lastValidTick > 500) state.valid = false;
    return state;
}

void SetZoom(float factor) {
    g_zoom = factor;
}

void SetGamma(float gamma) {
    g_gamma = gamma;
}

void SetDisplayGammaRamp(float gamma) {
    std::lock_guard lock(g_rampMutex);
    HWND hwnd = GetGameHwnd();
    if (!hwnd) return;
    HDC hdc = GetDC(hwnd);
    if (!hdc) return;

    if (!g_hasOriginalRamp) {
        if (GetDeviceGammaRamp(hdc, g_originalRamp)) {
            g_hasOriginalRamp = true;
        }
    }

    if (gamma <= 1.01f) {
        if (g_rampActive && g_hasOriginalRamp) {
            SetDeviceGammaRamp(hdc, g_originalRamp);
            g_rampActive = false;
        }
        ReleaseDC(hwnd, hdc);
        return;
    }

    WORD ramp[3][256];
    const double exponent = 1.0 / std::clamp(static_cast<double>(gamma) * 0.35 + 0.65, 1.0, 6.0);
    for (int i = 0; i < 256; ++i) {
        double v = i / 255.0;
        double boosted = std::pow(v, exponent);
        boosted = boosted * 0.82 + 0.18;
        WORD val = static_cast<WORD>(std::clamp(boosted * 65535.0, 0.0, 65535.0));
        ramp[0][i] = val;
        ramp[1][i] = val;
        ramp[2][i] = val;
    }
    if (SetDeviceGammaRamp(hdc, ramp)) {
        g_rampActive = true;
    }
    ReleaseDC(hwnd, hdc);
}

void RestoreDisplayGammaRamp() {
    std::lock_guard lock(g_rampMutex);
    if (!g_rampActive || !g_hasOriginalRamp) return;
    HWND hwnd = GetGameHwnd();
    if (!hwnd) return;
    HDC hdc = GetDC(hwnd);
    if (hdc) {
        SetDeviceGammaRamp(hdc, g_originalRamp);
        g_rampActive = false;
        ReleaseDC(hwnd, hdc);
    }
}

bool ZoomAvailable() {
    return g_zoomHooked && g_offsets.levelRendererPlayerFovX;
}

bool GammaAvailable() {
    return true; // Always available: native in-game memory hook, or hardware display gamma ramp fallback
}

bool IsGammaHooked() {
    return g_gammaHooked;
}

std::string CurrentScreen() {
    std::lock_guard lock(g_screenMutex);
    if (GetTickCount64() - g_lastScreenTick > 1000) return {};
    return g_currentScreen;
}

bool IsContainerOpen() {
    std::string s = CurrentScreen();
    if (s.empty()) return false;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s.find("inventory") != std::string::npos ||
           s.find("chest") != std::string::npos ||
           s.find("barrel") != std::string::npos ||
           s.find("shulker") != std::string::npos ||
           s.find("hopper") != std::string::npos ||
           s.find("dispenser") != std::string::npos ||
           s.find("dropper") != std::string::npos;
}

bool ChatAvailable() {
    return g_offsets.clientInstanceGuiData != 0;
}

bool PollChatCoords(ChatCoords& out) {
    std::lock_guard lock(g_chatMutex);
    if (g_chatCoords.empty()) return false;
    out = g_chatCoords.front();
    g_chatCoords.pop_front();
    return true;
}

Vec3 Forward(const GameState& state) {
    // Third row of the view matrix is the camera's back (or forward) axis in world space.
    Vec3 f{state.view[0 * 4 + 2], state.view[1 * 4 + 2], state.view[2 * 4 + 2]};
    const Vec3 ahead{state.origin.x + f.x * 10, state.origin.y + f.y * 10, state.origin.z + f.z * 10};
    float x, y;
    if (WorldToScreen(state, ahead, 100, 100, x, y) == false) f = {-f.x, -f.y, -f.z};
    return f;
}

float Heading(const GameState& state) {
    const Vec3 f = Forward(state);
    float deg = std::atan2(f.x, -f.z) * 57.29578f; // 0 = north (-Z), 90 = east (+X)
    if (deg < 0) deg += 360;
    return deg;
}

bool PollDeath(DeathInfo& out) {
    std::lock_guard lock(g_deathMutex);
    if (!g_deathPending) return false;
    g_deathPending = false;
    out = g_death;
    return true;
}

// Matrices are column-major: M[col * 4 + row]
static void ToViewSpace4(const GameState& cam, const Vec3& world, float out[4]) {
    const float rel[4] = {world.x - cam.origin.x, world.y - cam.origin.y, world.z - cam.origin.z, 1.0f};
    for (int r = 0; r < 4; ++r) {
        out[r] = 0;
        for (int c = 0; c < 4; ++c) out[r] += cam.view[c * 4 + r] * rel[c];
    }
}

Vec3 ToViewSpace(const GameState& cam, const Vec3& world) {
    float v[4];
    ToViewSpace4(cam, world, v);
    return {v[0], v[1], v[2]};
}

bool WorldToScreen(const GameState& cam, const Vec3& world, float screenW, float screenH, float& outX,
                   float& outY) {
    float viewSpace[4];
    ToViewSpace4(cam, world, viewSpace);
    float clip[4];
    for (int r = 0; r < 4; ++r) {
        clip[r] = 0;
        for (int c = 0; c < 4; ++c) clip[r] += cam.proj[c * 4 + r] * viewSpace[c];
    }

    if (clip[3] <= 0.01f) return false;
    const float ndcX = clip[0] / clip[3];
    const float ndcY = clip[1] / clip[3];
    outX = (ndcX + 1.0f) * 0.5f * screenW;
    outY = (1.0f - ndcY) * 0.5f * screenH;
    return true;
}

} // namespace game
