#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// Where the camera lives in Minecraft's memory. This changes between game versions,
// so there is one profile per version and the matching one is picked at startup.
struct Offsets {
    std::string name;
    // ScreenView::setupAndRender(ScreenView*, MinecraftUIRenderContext*)
    std::string setupAndRenderSig;
    bool sigIsCall = false; // the signature matches a `call setupAndRender` instead of the function itself

    uintptr_t uiContextClientInstance = 0;     // MinecraftUIRenderContext -> ClientInstance*
    uintptr_t clientInstanceMinecraftGame = 0; // ClientInstance -> MinecraftGame*
    uintptr_t minecraftGameGameRenderer = 0;   // MinecraftGame -> GameRenderer*
    uintptr_t gameRendererViewMatrix = 0;      // GameRenderer -> glm::mat4 view
    uintptr_t gameRendererProjMatrix = 0;      // GameRenderer -> glm::mat4 projection
    uintptr_t clientInstanceLevelRenderer = 0; // ClientInstance -> LevelRenderer* (0 = use the virtual below)
    uintptr_t clientInstanceGetLevelRenderer = 0; // ClientInstance::getLevelRenderer vtable index
    uintptr_t levelRendererPlayer = 0;            // LevelRenderer -> LevelRendererPlayer*
    uintptr_t levelRendererPlayerCameraPos = 0;   // LevelRendererPlayer -> Vec3 camera position
};

struct Config {
    int menuKey = 0x77; // VK_F8
    std::string profile = "auto";
    // Raw key=value pairs from config.ini that override the detected profile.
    std::map<std::string, std::string> overrides;
};

extern Config g_config;
extern Offsets g_offsets; // the profile in use, set by game::Init

const std::vector<Offsets>& BuiltInProfiles();
void ApplyOverrides(Offsets& offsets);

std::filesystem::path DataDir();
void LoadConfig();

// Appends a line to log.txt in DataDir().
void Log(const std::string& message);
