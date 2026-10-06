#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// Where things live in Minecraft's memory. This changes between game versions,
// so there is one profile per version and the matching one is picked at startup.
// An offset of 0 means "not known for this version": that feature is switched off.
struct Offsets {
    std::string name;
    // ScreenView::setupAndRender(ScreenView*, MinecraftUIRenderContext*)
    std::string setupAndRenderSig;
    bool sigIsCall = false; // the signature matches a `call setupAndRender` instead of the function itself

    // Camera
    uintptr_t uiContextClientInstance = 0;        // MinecraftUIRenderContext -> ClientInstance*
    uintptr_t clientInstanceMinecraftGame = 0;    // ClientInstance -> MinecraftGame*
    uintptr_t minecraftGameGameRenderer = 0;      // MinecraftGame -> GameRenderer*
    uintptr_t gameRendererViewMatrix = 0;         // GameRenderer -> glm::mat4 view
    uintptr_t gameRendererProjMatrix = 0;         // GameRenderer -> glm::mat4 projection
    uintptr_t clientInstanceLevelRenderer = 0;    // ClientInstance -> LevelRenderer* (0 = use the virtual below)
    uintptr_t clientInstanceGetLevelRenderer = 0; // ClientInstance::getLevelRenderer vtable index
    uintptr_t levelRendererPlayer = 0;            // LevelRenderer -> LevelRendererPlayer*
    uintptr_t levelRendererPlayerCameraPos = 0;   // LevelRendererPlayer -> Vec3 camera position

    // Player position and dimension
    uintptr_t clientInstanceGetLocalPlayer = 0; // ClientInstance::getLocalPlayer vtable index
    uintptr_t actorStateVector = 0;             // Actor -> StateVectorComponent* (Vec3 pos at +0)
    uintptr_t actorDimension = 0;               // Actor -> std::shared_ptr<Dimension>
    uintptr_t dimensionName = 0;                // Dimension -> std::string ("Overworld", "Nether", "TheEnd")

    // World name
    uintptr_t clientInstanceMinecraft = 0; // ClientInstance -> Minecraft*
    uintptr_t minecraftGameSession = 0;    // Minecraft -> GameSession*
    uintptr_t gameSessionHasLevel = 0;     // GameSession -> uint8_t, 1 when a level is loaded
    uintptr_t gameSessionLevelState = 0;   // GameSession -> uint8_t*, points at 1 while the level is alive
    uintptr_t gameSessionLevel = 0;        // GameSession -> Level*
    uintptr_t levelLevelData = 0;          // Level -> std::shared_ptr<LevelData>
    uintptr_t levelDataName = 0;           // LevelData -> std::string world name

    // Current screen (used to notice the death screen)
    uintptr_t screenViewVisualTree = 0; // ScreenView -> VisualTree*
    uintptr_t visualTreeRoot = 0;       // VisualTree -> UIControl*
    uintptr_t uiControlName = 0;        // UIControl -> std::string

    // Zoom: LevelRenderer::renderLevel(LevelRenderer*, ScreenContext*, void*)
    std::string renderLevelSig;
    bool renderLevelSigIsCall = false;
    uintptr_t levelRendererPlayerFovX = 0; // LevelRendererPlayer -> float
    uintptr_t levelRendererPlayerFovY = 0; // LevelRendererPlayer -> float

    // Fullbright: float Options::getGamma(Options*, void*)
    std::string gammaSig;

    // Chat
    uintptr_t clientInstanceGuiData = 0; // ClientInstance -> GuiData*
    uintptr_t guiDataMessages = 0;       // GuiData -> std::vector<GuiMessage>
    uintptr_t guiMessageSize = 0;        // sizeof(GuiMessage)
    uintptr_t guiMessageText = 0;        // GuiMessage -> std::string
};

struct Config {
    int menuKey = 0x77;        // VK_F8
    int addWaypointKey = 0x76; // VK_F7
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
