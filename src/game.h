#pragma once
#include <string>

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// Snapshot of the camera, taken on the game thread every UI frame.
// Matrices are column-major (glm layout), relative to `origin`.
struct CameraState {
    bool valid = false;
    Vec3 origin;
    float view[16]{};
    float proj[16]{};
};

namespace game {

// Finds the game function and hooks it. Returns false and fills `error` on failure.
bool Init(std::string& error);

// File version of Minecraft.Windows.exe, e.g. "1.26.5001.0".
std::string GameVersion();

CameraState Camera();

// Projects a world position to screen pixels. Returns false when the point is behind the camera.
bool WorldToScreen(const CameraState& cam, const Vec3& world, float screenW, float screenH, float& outX,
                   float& outY);

// Position relative to the camera (x = right, y = up). Used to point at off-screen waypoints.
Vec3 ToViewSpace(const CameraState& cam, const Vec3& world);

} // namespace game
