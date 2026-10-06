#include "game.h"

#include "config.h"

#include <windows.h>
#include <MinHook.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <vector>

namespace {

using SetupAndRenderFn = void(__fastcall*)(void* screenView, void* uiRenderContext);
SetupAndRenderFn g_originalSetupAndRender = nullptr;

std::mutex g_cameraMutex;
CameraState g_camera;
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

bool ReadCameraUnsafe(uintptr_t uiContext, CameraState& out) {
    const Config& c = g_config;
    const auto clientInstance = Read<uintptr_t>(uiContext + c.uiContextClientInstance);
    if (!clientInstance) return false;

    const auto minecraftGame = Read<uintptr_t>(clientInstance + c.clientInstanceMinecraftGame);
    if (!minecraftGame) return false;
    const auto gameRenderer = Read<uintptr_t>(minecraftGame + c.minecraftGameGameRenderer);
    if (!gameRenderer) return false;

    using GetLevelRendererFn = uintptr_t(__fastcall*)(uintptr_t);
    const auto vtable = Read<uintptr_t*>(clientInstance);
    const auto getLevelRenderer = reinterpret_cast<GetLevelRendererFn>(vtable[c.clientInstanceGetLevelRenderer]);
    const uintptr_t levelRenderer = getLevelRenderer(clientInstance);
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
    return std::isfinite(out.origin.x) && std::isfinite(out.origin.y) && std::isfinite(out.origin.z);
}

// Wrong offsets after a game update must not crash the game, so every read is guarded.
bool ReadCamera(uintptr_t uiContext, CameraState& out) {
#ifdef _MSC_VER
    __try {
        return ReadCameraUnsafe(uiContext, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    return ReadCameraUnsafe(uiContext, out);
#endif
}

void __fastcall SetupAndRenderDetour(void* screenView, void* uiRenderContext) {
    g_originalSetupAndRender(screenView, uiRenderContext);

    if (!uiRenderContext) return;
    CameraState cam;
    cam.valid = ReadCamera(reinterpret_cast<uintptr_t>(uiRenderContext), cam);

    std::lock_guard lock(g_cameraMutex);
    // setupAndRender runs once per screen layer; keep the last good snapshot.
    if (cam.valid) {
        g_camera = cam;
        g_lastValidTick = GetTickCount64();
    }
}

} // namespace

namespace game {

bool Init(std::string& error) {
    const auto address = FindPattern(g_config.setupAndRenderSig);
    if (!address) {
        error = "setupAndRenderSig not found (different Minecraft version?). Update config.ini.";
        return false;
    }
    if (MH_CreateHook(reinterpret_cast<void*>(*address), reinterpret_cast<void*>(&SetupAndRenderDetour),
                      reinterpret_cast<void**>(&g_originalSetupAndRender)) != MH_OK ||
        MH_EnableHook(reinterpret_cast<void*>(*address)) != MH_OK) {
        error = "Failed to hook setupAndRender.";
        return false;
    }
    return true;
}

CameraState Camera() {
    std::lock_guard lock(g_cameraMutex);
    CameraState cam = g_camera;
    // No fresh snapshot means we left the world (main menu, loading screen).
    if (GetTickCount64() - g_lastValidTick > 500) cam.valid = false;
    return cam;
}

// Matrices are column-major: M[col * 4 + row]
static void ToViewSpace4(const CameraState& cam, const Vec3& world, float out[4]) {
    const float rel[4] = {world.x - cam.origin.x, world.y - cam.origin.y, world.z - cam.origin.z, 1.0f};
    for (int r = 0; r < 4; ++r) {
        out[r] = 0;
        for (int c = 0; c < 4; ++c) out[r] += cam.view[c * 4 + r] * rel[c];
    }
}

Vec3 ToViewSpace(const CameraState& cam, const Vec3& world) {
    float v[4];
    ToViewSpace4(cam, world, v);
    return {v[0], v[1], v[2]};
}

bool WorldToScreen(const CameraState& cam, const Vec3& world, float screenW, float screenH, float& outX,
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
