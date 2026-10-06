#pragma once
#include "game.h"

#include <string>
#include <vector>

struct Waypoint {
    std::string name;
    Vec3 pos;
    float color[3] = {0.2f, 0.8f, 1.0f};
    bool visible = true;
};

// Only touched from the render thread.
extern std::vector<Waypoint> g_waypoints;

void LoadWaypoints();
void SaveWaypoints();
