#include "config.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>

Config g_config;
Offsets g_offsets;

namespace {

constexpr int kConfigVersion = 2;

struct NumericField {
    const char* key;
    uintptr_t Offsets::* member;
};

constexpr NumericField kNumericFields[] = {
    {"uiContextClientInstance", &Offsets::uiContextClientInstance},
    {"clientInstanceMinecraftGame", &Offsets::clientInstanceMinecraftGame},
    {"minecraftGameGameRenderer", &Offsets::minecraftGameGameRenderer},
    {"gameRendererViewMatrix", &Offsets::gameRendererViewMatrix},
    {"gameRendererProjMatrix", &Offsets::gameRendererProjMatrix},
    {"clientInstanceLevelRenderer", &Offsets::clientInstanceLevelRenderer},
    {"clientInstanceGetLevelRenderer", &Offsets::clientInstanceGetLevelRenderer},
    {"levelRendererPlayer", &Offsets::levelRendererPlayer},
    {"levelRendererPlayerCameraPos", &Offsets::levelRendererPlayerCameraPos},
    {"clientInstanceGetLocalPlayer", &Offsets::clientInstanceGetLocalPlayer},
    {"actorStateVector", &Offsets::actorStateVector},
    {"actorDimension", &Offsets::actorDimension},
    {"dimensionName", &Offsets::dimensionName},
    {"clientInstanceMinecraft", &Offsets::clientInstanceMinecraft},
    {"minecraftGameSession", &Offsets::minecraftGameSession},
    {"gameSessionHasLevel", &Offsets::gameSessionHasLevel},
    {"gameSessionLevelState", &Offsets::gameSessionLevelState},
    {"gameSessionLevel", &Offsets::gameSessionLevel},
    {"levelLevelData", &Offsets::levelLevelData},
    {"levelDataName", &Offsets::levelDataName},
    {"screenViewVisualTree", &Offsets::screenViewVisualTree},
    {"visualTreeRoot", &Offsets::visualTreeRoot},
    {"uiControlName", &Offsets::uiControlName},
};

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

std::string Hex(uintptr_t v) {
    char text[32];
    snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(v));
    return text;
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

void WriteDefaultConfig(const std::filesystem::path& path) {
    std::string profiles;
    for (const auto& p : BuiltInProfiles()) profiles += ", " + p.name;

    const Offsets& d = BuiltInProfiles().front();
    std::ofstream f(path);
    f << "; Bedrock Waypoints config\n"
      << "configVersion=" << kConfigVersion << "\n\n"
      << "; Virtual-key codes (0x76 = F7, 0x77 = F8, 0x2D = Insert, 0xA1 = Right Shift)\n"
      << "menuKey=" << Hex(g_config.menuKey) << "\n"
      << "addWaypointKey=" << Hex(g_config.addWaypointKey) << "\n\n"
      << "; Which Minecraft version profile to use: auto" << profiles << "\n"
      << "profile=auto\n\n"
      << "; After a Minecraft update the built-in profiles can stop working.\n"
      << "; Remove the ; in front of a line below to override that value. Delete this file to reset.\n"
      << ";setupAndRenderSig=" << d.setupAndRenderSig << "\n"
      << ";sigIsCall=" << (d.sigIsCall ? 1 : 0) << "\n"
      << "; Values ending in GetLevelRenderer/GetLocalPlayer are vtable indexes.\n";
    for (const auto& field : kNumericFields) f << ";" << field.key << "=" << Hex(d.*field.member) << "\n";
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
        v2650.clientInstanceGetLocalPlayer = 0x1F;
        v2650.actorStateVector = 0x218;
        v2650.actorDimension = 0x1C8;
        v2650.dimensionName = 0x20;
        v2650.clientInstanceMinecraft = 0x1B0;
        v2650.minecraftGameSession = 0xC8;
        v2650.gameSessionHasLevel = 0x28;
        v2650.gameSessionLevelState = 0x30;
        v2650.gameSessionLevel = 0x40;
        v2650.levelLevelData = 0x90;
        v2650.levelDataName = 0x2A8;
        v2650.screenViewVisualTree = 0x50;
        v2650.visualTreeRoot = 0x8;
        v2650.uiControlName = 0x20;
        list.push_back(v2650);

        // Values from the Flarial client (github.com/flarialmc/dll). Camera only.
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
    for (const auto& field : kNumericFields) {
        const auto it = g_config.overrides.find(field.key);
        if (it != g_config.overrides.end()) ParseNumber(it->second, o.*field.member);
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
    if (auto it = values.find("addWaypointKey"); it != values.end() && ParseNumber(it->second, number)) {
        g_config.addWaypointKey = static_cast<int>(number);
    }

    // Older config files listed every offset uncommented, which would pin the old version's values.
    if (!values.count("configVersion") || values["configVersion"] != std::to_string(kConfigVersion)) {
        std::error_code ec;
        if (std::filesystem::exists(path)) {
            std::filesystem::rename(path, DataDir() / L"config.old.ini", ec);
            Log("Old config.ini moved to config.old.ini");
        }
        WriteDefaultConfig(path);
        return;
    }

    if (auto it = values.find("profile"); it != values.end()) g_config.profile = it->second;
    for (const char* key : {"configVersion", "menuKey", "addWaypointKey", "profile"}) values.erase(key);
    g_config.overrides = std::move(values);
}
