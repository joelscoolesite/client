#include "config.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>

Config g_config;
Offsets g_offsets;

namespace {

constexpr int kConfigVersion = 2;

std::string Trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool ParseNumber(const std::string& value, uintptr_t& out) {
    try {
        out = static_cast<uintptr_t>(std::stoull(value, nullptr, 0));
        return true;
    } catch (...) {
        return false;
    }
}

// key=value lines, ignoring blank lines and ; or # comments.
std::map<std::string, std::string> ReadIni(const std::filesystem::path& path) {
    std::map<std::string, std::string> values;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string value = Trim(line.substr(eq + 1));
        if (!value.empty()) values[Trim(line.substr(0, eq))] = value;
    }
    return values;
}

void WriteDefaultConfig(const std::filesystem::path& path, int menuKey) {
    std::string profiles;
    for (const auto& p : BuiltInProfiles()) profiles += ", " + p.name;

    char key[16];
    snprintf(key, sizeof(key), "0x%X", menuKey);

    const Offsets& d = BuiltInProfiles().front();
    std::ofstream f(path);
    f << "; Bedrock Waypoints config\n"
      << "configVersion=" << kConfigVersion << "\n\n"
      << "; Virtual-key code of the menu key (0x77 = F8, 0x2D = Insert, 0xA1 = Right Shift)\n"
      << "menuKey=" << key << "\n\n"
      << "; Which Minecraft version profile to use: auto" << profiles << "\n"
      << "profile=auto\n\n"
      << "; After a Minecraft update the built-in profiles can stop working.\n"
      << "; Remove the ; in front of a line below to override that value. Delete this file to reset.\n"
      << ";setupAndRenderSig=" << d.setupAndRenderSig << "\n"
      << ";sigIsCall=" << (d.sigIsCall ? 1 : 0) << "\n"
      << ";uiContextClientInstance=0x" << std::hex << std::uppercase << d.uiContextClientInstance << "\n"
      << ";clientInstanceMinecraftGame=0x" << d.clientInstanceMinecraftGame << "\n"
      << ";minecraftGameGameRenderer=0x" << d.minecraftGameGameRenderer << "\n"
      << ";gameRendererViewMatrix=0x" << d.gameRendererViewMatrix << "\n"
      << ";gameRendererProjMatrix=0x" << d.gameRendererProjMatrix << "\n"
      << "; LevelRenderer member offset, or 0 to call the virtual function clientInstanceGetLevelRenderer (decimal index)\n"
      << ";clientInstanceLevelRenderer=0x" << d.clientInstanceLevelRenderer << "\n"
      << std::dec << ";clientInstanceGetLevelRenderer=" << d.clientInstanceGetLevelRenderer << "\n"
      << ";levelRendererPlayer=0x" << std::hex << d.levelRendererPlayer << "\n"
      << ";levelRendererPlayerCameraPos=0x" << d.levelRendererPlayerCameraPos << "\n";
}

} // namespace

const std::vector<Offsets>& BuiltInProfiles() {
    static const std::vector<Offsets> profiles = [] {
        std::vector<Offsets> list;

        // Values from the Latite client (github.com/LatiteClient/Latite).
        Offsets v2650;
        v2650.name = "1.26.5x";
        v2650.setupAndRenderSig = "E8 ? ? ? ? 48 8B 4B ? 48 85 C9 74 ? 48 8B 01 48 8B 40 ? 48 89 FA FF 15 ? ? ? ? 48 8D 4D";
        v2650.sigIsCall = true;
        v2650.uiContextClientInstance = 0x8;
        v2650.clientInstanceMinecraftGame = 0x1A8;
        v2650.minecraftGameGameRenderer = 0x1440;
        v2650.gameRendererViewMatrix = 0x388;
        v2650.gameRendererProjMatrix = 0x408;
        v2650.clientInstanceLevelRenderer = 0x1C0;
        v2650.levelRendererPlayer = 0x468;
        v2650.levelRendererPlayerCameraPos = 0x660;
        list.push_back(v2650);

        // Values from the Flarial client (github.com/flarialmc/dll).
        Offsets v260;
        v260.name = "1.26.0-1.26.3";
        v260.setupAndRenderSig =
            "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? "
            "0F 29 BC 24 ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B FA";
        v260.uiContextClientInstance = 0x8;
        v260.clientInstanceMinecraftGame = 0x1A0;
        v260.minecraftGameGameRenderer = 0xD70;
        v260.gameRendererViewMatrix = 0x358;
        v260.gameRendererProjMatrix = 0x3D8;
        v260.clientInstanceGetLevelRenderer = 187;
        v260.levelRendererPlayer = 0x430;
        v260.levelRendererPlayerCameraPos = 0x704;
        list.push_back(v260);

        return list;
    }();
    return profiles;
}

void ApplyOverrides(Offsets& o) {
    const std::pair<const char*, uintptr_t*> numbers[] = {
        {"uiContextClientInstance", &o.uiContextClientInstance},
        {"clientInstanceMinecraftGame", &o.clientInstanceMinecraftGame},
        {"minecraftGameGameRenderer", &o.minecraftGameGameRenderer},
        {"gameRendererViewMatrix", &o.gameRendererViewMatrix},
        {"gameRendererProjMatrix", &o.gameRendererProjMatrix},
        {"clientInstanceLevelRenderer", &o.clientInstanceLevelRenderer},
        {"clientInstanceGetLevelRenderer", &o.clientInstanceGetLevelRenderer},
        {"levelRendererPlayer", &o.levelRendererPlayer},
        {"levelRendererPlayerCameraPos", &o.levelRendererPlayerCameraPos},
    };
    for (const auto& [key, field] : numbers) {
        const auto it = g_config.overrides.find(key);
        if (it != g_config.overrides.end()) ParseNumber(it->second, *field);
    }
    if (const auto it = g_config.overrides.find("setupAndRenderSig"); it != g_config.overrides.end()) {
        o.setupAndRenderSig = it->second;
    }
    if (const auto it = g_config.overrides.find("sigIsCall"); it != g_config.overrides.end()) {
        o.sigIsCall = it->second != "0";
    }
}

std::filesystem::path DataDir() {
    static std::filesystem::path dir = [] {
        std::filesystem::path base;
        wchar_t local[MAX_PATH]{};
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            base = local;
        } else {
            base = std::filesystem::temp_directory_path();
        }
        std::filesystem::path d = base / L"BedrockWaypoints";
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        return d;
    }();
    return dir;
}

void Log(const std::string& message) {
    std::ofstream f(DataDir() / L"log.txt", std::ios::app);
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[32];
    snprintf(stamp, sizeof(stamp), "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    f << stamp << message << "\n";
}

void LoadConfig() {
    const auto path = DataDir() / L"config.ini";
    auto values = ReadIni(path);

    uintptr_t number = 0;
    if (auto it = values.find("menuKey"); it != values.end() && ParseNumber(it->second, number)) {
        g_config.menuKey = static_cast<int>(number);
    }

    // Older config files listed every offset uncommented, which would pin the old version's values.
    if (!values.count("configVersion") || values["configVersion"] != std::to_string(kConfigVersion)) {
        std::error_code ec;
        if (std::filesystem::exists(path)) {
            std::filesystem::rename(path, DataDir() / L"config.old.ini", ec);
            Log("Old config.ini moved to config.old.ini");
        }
        WriteDefaultConfig(path, g_config.menuKey);
        return;
    }

    if (auto it = values.find("profile"); it != values.end()) g_config.profile = it->second;
    for (const char* key : {"configVersion", "menuKey", "profile"}) values.erase(key);
    g_config.overrides = std::move(values);
}
