#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

// Everything that changes between Minecraft versions lives here so it can be
// fixed in config.ini without recompiling. Defaults are for 1.26.x.
struct Config {
    int menuKey = 0x77; // VK_F8

    // ScreenView::setupAndRender(ScreenView*, MinecraftUIRenderContext*)
    std::string setupAndRenderSig =
        "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? "
        "0F 29 BC 24 ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B FA";

    uintptr_t uiContextClientInstance = 0x8;    // MinecraftUIRenderContext -> ClientInstance*
    uintptr_t clientInstanceMinecraftGame = 0x1A0; // ClientInstance -> MinecraftGame*
    uintptr_t minecraftGameGameRenderer = 0xD70;   // MinecraftGame -> GameRenderer*
    uintptr_t gameRendererViewMatrix = 0x358;      // GameRenderer -> glm::mat4 view
    uintptr_t gameRendererProjMatrix = 0x3D8;      // GameRenderer -> glm::mat4 projection
    uintptr_t clientInstanceGetLevelRenderer = 187; // virtual function index
    uintptr_t levelRendererPlayer = 0x430;         // LevelRenderer -> LevelRendererPlayer*
    uintptr_t levelRendererPlayerCameraPos = 0x704; // LevelRendererPlayer -> Vec3 camera position
};

extern Config g_config;

std::filesystem::path DataDir();
void LoadConfig();

// Appends a line to log.txt in DataDir().
void Log(const std::string& message);
