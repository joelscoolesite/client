#pragma once
#include "game.h"

#include <string>
#include <vector>

struct Waypoint {
    std::string name;
    Vec3 pos; // block coordinates of the feet
    float color[3] = {0.2f, 0.8f, 1.0f};
    bool visible = true;
    std::string world;     // world it belongs to, empty = unknown
    std::string dimension; // "Overworld", "Nether", "TheEnd", empty = unknown
    bool always = false;   // show in every world, every time you inject
    bool death = false;    // created automatically when you died
};

struct Settings {
    bool showBeam = true;
    bool showDistance = true;
    bool netherConversion = true; // show overworld waypoints in the nether (/8) and back (*8)
    bool deathWaypoints = true;
    int maxDeathWaypoints = 3;
};

// Only touched from the render thread.
extern std::vector<Waypoint> g_waypoints;
extern Settings g_settings;

void LoadWaypoints();
void SaveWaypoints();
void LoadSettings();
void SaveSettings();

// Friendly name for a dimension id: "Overworld", "Nether", "The End".
const char* DimensionLabel(const std::string& dimension);
