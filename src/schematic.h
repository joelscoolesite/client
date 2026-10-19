#pragma once
#include "game.h"

#include <imgui.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct SchematicBlock {
    int x = 0, y = 0, z = 0; // coordinates relative to schematic origin
    std::string name = "stone";
    ImU32 color = IM_COL32(180, 180, 180, 220);
};

class Schematic {
public:
    std::string name = "None";
    int sizeX = 0, sizeY = 0, sizeZ = 0;
    Vec3 origin{0, 64, 0}; // world coordinates of minimum corner
    int rotation = 0;      // 0, 90, 180, 270
    int minLayer = 0;
    int maxLayer = 256;
    bool allLayers = true;
    bool showBoundingBox = true;
    float renderDistance = 64.0f;

    std::vector<SchematicBlock> blocks;
    std::map<std::string, int> materials;

    void Clear();
    void Rotate90();
    void RecalculateBoundsAndMaterials();

    bool LoadFromFile(const std::filesystem::path& path);
    bool SaveToFile(const std::filesystem::path& path) const;
};

namespace schematic {

void Init();
Schematic& Current();
std::vector<std::string> AvailableTemplates();
std::vector<std::string> AvailableFiles();

void LoadTemplate(const std::string& templateName);
bool LoadFile(const std::string& fileName);

// Built-in generators
void GenerateNetherPortal();
void GenerateEnchantingRoom();
void GenerateBeaconPyramid(int tiers = 4);
void GenerateStarterHouse();
void GenerateMobFarm();
void GenerateCircle(int radius, int height, const std::string& block = "stone");
void GenerateHollowBox(int width, int height, int depth, const std::string& block = "stone");
void GenerateSphere(int radius, const std::string& block = "glass");

// 3D rendering in the world
void Draw3D(ImDrawList* dl, const GameState& state, ImVec2 screen);

// ClickGUI tab
void DrawPage(const GameState& state);

std::filesystem::path SchematicDir();

} // namespace schematic
