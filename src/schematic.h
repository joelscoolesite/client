#pragma once
#include "game.h"
#include "litematic.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct ImDrawList;
struct ImVec2;

// Where the loaded schematic sits in the world.
struct SchematicPlacement {
    std::string file;           // file name inside the schematics folder
    int origin[3] = {0, 64, 0}; // block where the schematic's lowest corner goes
    int rotation = 0;           // quarter turns clockwise, seen from above
    bool mirror = false;        // flip along X before rotating
    bool visible = true;
    std::string world;     // world it was placed in, empty = any
    std::string dimension; // dimension it was placed in, empty = any
};

enum class LayerMode { All, Single, Below };

struct SchematicView {
    float renderDistance = 32;
    float opacity = 0.3f;
    bool outlines = true;
    LayerMode layerMode = LayerMode::All;
    int layer = 0;             // relative to the schematic's bottom
    bool followPlayer = false; // layer = the height your feet are at
};

// Render thread only.
namespace schematic {

extern SchematicPlacement g_placement;
extern SchematicView g_view;

std::filesystem::path Folder(); // DataDir()/schematics
std::vector<std::string> Files();

void Init();             // loads the saved placement and its schematic
void Save();             // stores placement and view settings
void Load(const std::string& file); // loads in the background, see Loading()
void Unload();
bool Loading();
std::string LastError();
const Schematic* Current(); // nullptr when nothing is loaded

// Call after editing g_placement origin/rotation/mirror.
void PlacementChanged();
// Size after rotation (x, y, z).
void RotatedSize(int out[3]);
// The layer that is shown when a layer mode is active (follows the player if asked).
int ActiveLayer(const GameState& state);

// Block counts, most first. Only the visible layer(s) when `visibleOnly` is set.
std::vector<std::pair<std::string, int>> Materials(const GameState& state, bool visibleOnly);

void DrawWorld(ImDrawList* dl, const GameState& state, ImVec2 screen);

} // namespace schematic
