#pragma once
#include <cstdint>
#include <string>

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

constexpr float kEyeHeight = 1.62f;

struct ItemInfo {
    char id[48]{};         // "diamond_helmet", empty when the slot is empty
    int count = 0;
    int durability = -1;   // uses left, -1 when the item does not wear out (or it is unknown)
    int maxDurability = 0;
};

// Snapshot of the game, taken on the game thread every UI frame.
// Matrices are column-major (glm layout), relative to `origin`.
struct GameState {
    bool valid = false; // camera data is usable
    Vec3 origin;
    float view[16]{};
    float proj[16]{};

    bool hasPlayer = false;
    Vec3 playerPos;       // eye position, feet are kEyeHeight lower
    char dimension[32]{}; // "Overworld", "Nether", "TheEnd" or empty when unknown
    char world[128]{};    // world name or empty when unknown
    ItemInfo mainhand;    // selected hotbar slot
    ItemInfo offhand;
    ItemInfo armor[4];    // helmet, chestplate, leggings, boots
    int totems = 0;       // totems of undying in the inventory and offhand
    bool hasInventory = false;
    bool hasEquipment = false; // offhand and armor could be read
    bool mouseGrabbed = true; // false while a game screen (inventory, chat, pause) has the mouse
};

struct DeathInfo {
    Vec3 feetPos;
    char dimension[32]{};
    char world[128]{};
};

struct ChatCoords {
    Vec3 pos;
    std::string text; // the chat line it came from
};

namespace game {

// Finds the game function and hooks it. Returns false and fills `error` on failure.
bool Init(std::string& error);

// File version of Minecraft.Windows.exe, e.g. "1.26.5001.0".
std::string GameVersion();

GameState State();

// Zoom factor (1 = normal) and gamma override (< 0 = game value). Safe from any thread.
void SetZoom(float factor);
void SetGamma(float gamma);
// While true the game lets go of the mouse (menu open). Safe from any thread.
void SetCursorFree(bool free);
bool ZoomAvailable();
bool GammaAvailable();
bool ChatAvailable();
bool HeldItemAvailable();
bool EquipmentAvailable();

// Coordinates someone typed in chat, oldest first.
bool PollChatCoords(ChatCoords& out);

// Direction the camera looks at (unit vector).
Vec3 Forward(const GameState& state);

// Horizontal facing in degrees: 0 = north, 90 = east, 180 = south, 270 = west.
float Heading(const GameState& state);

// Returns true once for every time the death screen appears.
bool PollDeath(DeathInfo& out);

// Projects a world position to screen pixels. Returns false when the point is behind the camera.
bool WorldToScreen(const GameState& state, const Vec3& world, float screenW, float screenH, float& outX,
                   float& outY);

// Position relative to the camera (x = right, y = up). Used to point at off-screen waypoints.
Vec3 ToViewSpace(const GameState& state, const Vec3& world);

} // namespace game
