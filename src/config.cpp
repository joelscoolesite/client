#include "config.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <type_traits>

Config g_config;

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

static std::string Trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string Hex(uintptr_t v) {
    std::ostringstream o;
    o << "0x" << std::hex << std::uppercase << v;
    return o.str();
}

static void WriteDefaultConfig(const std::filesystem::path& path) {
    const Config d;
    std::ofstream f(path);
    f << "; Bedrock Waypoints config\n"
         "; A Minecraft update can break the signature/offsets below.\n"
         "; Fix them here, no need to rebuild the DLL. Delete this file to restore defaults.\n\n"
         "; Virtual-key code of the menu key (0x77 = F8, 0x2D = Insert, 0xA1 = Right Shift)\n"
      << "menuKey=" << Hex(d.menuKey) << "\n\n"
      << "setupAndRenderSig=" << d.setupAndRenderSig << "\n"
      << "uiContextClientInstance=" << Hex(d.uiContextClientInstance) << "\n"
      << "clientInstanceMinecraftGame=" << Hex(d.clientInstanceMinecraftGame) << "\n"
      << "minecraftGameGameRenderer=" << Hex(d.minecraftGameGameRenderer) << "\n"
      << "gameRendererViewMatrix=" << Hex(d.gameRendererViewMatrix) << "\n"
      << "gameRendererProjMatrix=" << Hex(d.gameRendererProjMatrix) << "\n"
      << "; vtable index (decimal)\n"
      << "clientInstanceGetLevelRenderer=" << d.clientInstanceGetLevelRenderer << "\n"
      << "levelRendererPlayer=" << Hex(d.levelRendererPlayer) << "\n"
      << "levelRendererPlayerCameraPos=" << Hex(d.levelRendererPlayerCameraPos) << "\n";
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
    if (!std::filesystem::exists(path)) {
        WriteDefaultConfig(path);
        return;
    }

    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        if (value.empty()) continue;

        auto num = [&](auto& out) {
            try {
                out = static_cast<std::remove_reference_t<decltype(out)>>(std::stoull(value, nullptr, 0));
            } catch (...) {
            }
        };

        if (key == "menuKey") num(g_config.menuKey);
        else if (key == "setupAndRenderSig") g_config.setupAndRenderSig = value;
        else if (key == "uiContextClientInstance") num(g_config.uiContextClientInstance);
        else if (key == "clientInstanceMinecraftGame") num(g_config.clientInstanceMinecraftGame);
        else if (key == "minecraftGameGameRenderer") num(g_config.minecraftGameGameRenderer);
        else if (key == "gameRendererViewMatrix") num(g_config.gameRendererViewMatrix);
        else if (key == "gameRendererProjMatrix") num(g_config.gameRendererProjMatrix);
        else if (key == "clientInstanceGetLevelRenderer") num(g_config.clientInstanceGetLevelRenderer);
        else if (key == "levelRendererPlayer") num(g_config.levelRendererPlayer);
        else if (key == "levelRendererPlayerCameraPos") num(g_config.levelRendererPlayerCameraPos);
    }
}
