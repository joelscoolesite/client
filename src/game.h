#pragma once
#include <cstdint>
#include <string>

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

constexpr float kEyeHeight = 1.62f;

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
};

struct DeathInfo {
    Vec3 feetPos;
    char dimension[32]{};
    char world[128]{};
};

namespace game {

// Finds the game function and hooks it. Returns false and fills `error` on failure.
bool Init(std::string& error);

// File version of Minecraft.Windows.exe, e.g. "1.26.5001.0".
std::string GameVersion();

GameState State();

// Returns true once for every time the death screen appears.
bool PollDeath(DeathInfo& out);

// Projects a world position to screen pixels. Returns false when the point is behind the camera.
bool WorldToScreen(const GameState& state, const Vec3& world, float screenW, float screenH, float& outX,
                   float& outY);

// Position relative to the camera (x = right, y = up). Used to point at off-screen waypoints.
Vec3 ToViewSpace(const GameState& state, const Vec3& world);

} // namespace game
