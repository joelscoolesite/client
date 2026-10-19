#include "schematic.h"

#include "config.h"
#include "modules.h"
#include "ui.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {

Schematic s_current;

ImU32 BlockColorFromName(const std::string& rawName) {
    std::string name = rawName;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (name.find("obsidian") != std::string::npos) return IM_COL32(130, 50, 190, 220);
    if (name.find("portal") != std::string::npos) return IM_COL32(190, 80, 255, 180);
    if (name.find("bookshelf") != std::string::npos) return IM_COL32(165, 110, 55, 230);
    if (name.find("glass") != std::string::npos) return IM_COL32(160, 230, 255, 160);
    if (name.find("diamond") != std::string::npos) return IM_COL32(80, 240, 240, 240);
    if (name.find("gold") != std::string::npos) return IM_COL32(255, 215, 0, 240);
    if (name.find("iron") != std::string::npos) return IM_COL32(220, 220, 230, 240);
    if (name.find("emerald") != std::string::npos) return IM_COL32(50, 230, 90, 240);
    if (name.find("netherite") != std::string::npos) return IM_COL32(80, 70, 75, 240);
    if (name.find("beacon") != std::string::npos) return IM_COL32(130, 250, 240, 240);
    if (name.find("wood") != std::string::npos || name.find("plank") != std::string::npos || name.find("log") != std::string::npos)
        return IM_COL32(185, 135, 80, 230);
    if (name.find("brick") != std::string::npos) return IM_COL32(190, 90, 70, 230);
    if (name.find("stone") != std::string::npos || name.find("cobble") != std::string::npos)
        return IM_COL32(150, 150, 150, 220);
    if (name.find("dirt") != std::string::npos) return IM_COL32(134, 96, 67, 230);
    if (name.find("leaf") != std::string::npos || name.find("leaves") != std::string::npos)
        return IM_COL32(60, 160, 60, 200);
    if (name.find("water") != std::string::npos) return IM_COL32(50, 120, 230, 170);
    if (name.find("lava") != std::string::npos) return IM_COL32(240, 120, 30, 220);
    if (name.find("redstone") != std::string::npos) return IM_COL32(230, 40, 40, 230);

    return IM_COL32(120, 175, 255, 210);
}

void WorldLineDirect(ImDrawList* dl, const GameState& s, Vec3 a, Vec3 b, ImU32 color, float thickness, ImVec2 screen) {
    auto toClip = [&](const Vec3& p, float out[4]) {
        const float rel[4] = {p.x - s.origin.x, p.y - s.origin.y, p.z - s.origin.z, 1.0f};
        float v[4];
        for (int r = 0; r < 4; ++r) {
            v[r] = 0;
            for (int c = 0; c < 4; ++c) v[r] += s.view[c * 4 + r] * rel[c];
        }
        for (int r = 0; r < 4; ++r) {
            out[r] = 0;
            for (int c = 0; c < 4; ++c) out[r] += s.proj[c * 4 + r] * v[c];
        }
    };

    float ca[4], cb[4];
    toClip(a, ca);
    toClip(b, cb);
    constexpr float kNear = 0.05f;
    if (ca[3] < kNear && cb[3] < kNear) return;

    auto cut = [](float* from, const float* to) {
        const float t = (kNear - from[3]) / (to[3] - from[3]);
        for (int i = 0; i < 4; ++i) from[i] += (to[i] - from[i]) * t;
    };
    if (ca[3] < kNear) cut(ca, cb);
    if (cb[3] < kNear) cut(cb, ca);

    auto toScreen = [&](const float* c) {
        return ImVec2((c[0] / c[3] + 1) * 0.5f * screen.x, (1 - c[1] / c[3]) * 0.5f * screen.y);
    };
    dl->AddLine(toScreen(ca), toScreen(cb), color, thickness);
}

void DrawBlockWireframe(ImDrawList* dl, const GameState& s, const Vec3& pos, ImU32 color, float thickness, ImVec2 screen) {
    const float x0 = pos.x, y0 = pos.y, z0 = pos.z;
    const float x1 = x0 + 1.0f, y1 = y0 + 1.0f, z1 = z0 + 1.0f;

    // Bottom square
    WorldLineDirect(dl, s, {x0, y0, z0}, {x1, y0, z0}, color, thickness, screen);
    WorldLineDirect(dl, s, {x1, y0, z0}, {x1, y0, z1}, color, thickness, screen);
    WorldLineDirect(dl, s, {x1, y0, z1}, {x0, y0, z1}, color, thickness, screen);
    WorldLineDirect(dl, s, {x0, y0, z1}, {x0, y0, z0}, color, thickness, screen);

    // Top square
    WorldLineDirect(dl, s, {x0, y1, z0}, {x1, y1, z0}, color, thickness, screen);
    WorldLineDirect(dl, s, {x1, y1, z0}, {x1, y1, z1}, color, thickness, screen);
    WorldLineDirect(dl, s, {x1, y1, z1}, {x0, y1, z1}, color, thickness, screen);
    WorldLineDirect(dl, s, {x0, y1, z1}, {x0, y1, z0}, color, thickness, screen);

    // Vertical pillars
    WorldLineDirect(dl, s, {x0, y0, z0}, {x0, y1, z0}, color, thickness, screen);
    WorldLineDirect(dl, s, {x1, y0, z0}, {x1, y1, z0}, color, thickness, screen);
    WorldLineDirect(dl, s, {x1, y0, z1}, {x1, y1, z1}, color, thickness, screen);
    WorldLineDirect(dl, s, {x0, y0, z1}, {x0, y1, z1}, color, thickness, screen);
}

void DrawVolumeOutline(ImDrawList* dl, const GameState& s, const Vec3& origin, int sx, int sy, int sz, ImU32 color, float thickness, ImVec2 screen) {
    const float x0 = origin.x, y0 = origin.y, z0 = origin.z;
    const float x1 = x0 + static_cast<float>(sx);
    const float y1 = y0 + static_cast<float>(sy);
    const float z1 = z0 + static_cast<float>(sz);

    // Bottom
    WorldLineDirect(dl, s, {x0, y0, z0}, {x1, y0, z0}, color, thickness, screen);
    WorldLineDirect(dl, s, {x1, y0, z0}, {x1, y0, z1}, color, thickness, screen);
    WorldLineDirect(dl, s, {x1, y0, z1}, {x0, y0, z1}, color, thickness, screen);
    WorldLineDirect(dl, s, {x0, y0, z1}, {x0, y0, z0}, color, thickness, screen);

    // Top
    WorldLineDirect(dl, s, {x0, y1, z0}, {x1, y1, z0}, color, thickness, screen);
    WorldLineDirect(dl, s, {x1, y1, z0}, {x1, y1, z1}, color, thickness, screen);
    WorldLineDirect(dl, s, {x1, y1, z1}, {x0, y1, z1}, color, thickness, screen);
    WorldLineDirect(dl, s, {x0, y1, z1}, {x0, y1, z0}, color, thickness, screen);

    // Columns
    WorldLineDirect(dl, s, {x0, y0, z0}, {x0, y1, z0}, color, thickness, screen);
    WorldLineDirect(dl, s, {x1, y0, z0}, {x1, y1, z0}, color, thickness, screen);
    WorldLineDirect(dl, s, {x1, y0, z1}, {x1, y1, z1}, color, thickness, screen);
    WorldLineDirect(dl, s, {x0, y0, z1}, {x0, y1, z1}, color, thickness, screen);
}

} // namespace

void Schematic::Clear() {
    blocks.clear();
    materials.clear();
    sizeX = sizeY = sizeZ = 0;
}

void Schematic::Rotate90() {
    if (blocks.empty()) return;
    rotation = (rotation + 90) % 360;

    // Rotate block coordinates 90 degrees around Y: (x, z) -> (z, sizeX - 1 - x)
    const int oldSizeX = sizeX;
    for (auto& b : blocks) {
        const int oldX = b.x;
        b.x = b.z;
        b.z = (oldSizeX > 0 ? (oldSizeX - 1 - oldX) : -oldX);
    }
    RecalculateBoundsAndMaterials();
}

void Schematic::RecalculateBoundsAndMaterials() {
    materials.clear();
    if (blocks.empty()) {
        sizeX = sizeY = sizeZ = 0;
        return;
    }

    int minX = blocks[0].x, maxX = blocks[0].x;
    int minY = blocks[0].y, maxY = blocks[0].y;
    int minZ = blocks[0].z, maxZ = blocks[0].z;

    for (const auto& b : blocks) {
        minX = std::min(minX, b.x);
        maxX = std::max(maxX, b.x);
        minY = std::min(minY, b.y);
        maxY = std::max(maxY, b.y);
        minZ = std::min(minZ, b.z);
        maxZ = std::max(maxZ, b.z);
        materials[b.name]++;
    }

    // Normalize so minimum corner is at (0, 0, 0)
    if (minX != 0 || minY != 0 || minZ != 0) {
        for (auto& b : blocks) {
            b.x -= minX;
            b.y -= minY;
            b.z -= minZ;
        }
        maxX -= minX;
        maxY -= minY;
        maxZ -= minZ;
    }

    sizeX = maxX + 1;
    sizeY = maxY + 1;
    sizeZ = maxZ + 1;
}

bool Schematic::LoadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    Clear();
    name = path.stem().string();

    std::string line;
    while (std::getline(file, line)) {
        // Strip comments and whitespace
        const auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        const auto trimB = line.find_first_not_of(" \t\r\n");
        if (trimB == std::string::npos) continue;
        const auto trimE = line.find_last_not_of(" \t\r\n");
        line = line.substr(trimB, trimE - trimB + 1);
        if (line.empty()) continue;

        // Key=Value metadata
        if (line.rfind("name=", 0) == 0) {
            name = line.substr(5);
            continue;
        }

        // CSV line format: x,y,z,block_name
        std::stringstream ss(line);
        std::string sx, sy, sz, bname;
        if (std::getline(ss, sx, ',') && std::getline(ss, sy, ',') && std::getline(ss, sz, ',') && std::getline(ss, bname)) {
            try {
                SchematicBlock b;
                b.x = std::stoi(sx);
                b.y = std::stoi(sy);
                b.z = std::stoi(sz);
                b.name = bname;
                b.color = BlockColorFromName(bname);
                blocks.push_back(b);
            } catch (...) {}
        }
    }

    RecalculateBoundsAndMaterials();
    return !blocks.empty();
}

bool Schematic::SaveToFile(const std::filesystem::path& path) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "# Nova Client Blueprint file\n";
    file << "name=" << name << "\n";
    file << "# Format: x,y,z,block_name\n";
    for (const auto& b : blocks) {
        file << b.x << "," << b.y << "," << b.z << "," << b.name << "\n";
    }
    return true;
}

namespace schematic {

std::filesystem::path SchematicDir() {
    auto dir = DataDir() / L"schematics";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void Init() {
    auto dir = SchematicDir();
    // Pre-populate with default blueprints if empty
    std::error_code ec;
    if (std::filesystem::is_empty(dir, ec)) {
        Schematic portal;
        portal.name = "Nether Portal";
        GenerateNetherPortal();
        s_current.SaveToFile(dir / L"Nether_Portal.blueprint");

        GenerateEnchantingRoom();
        s_current.SaveToFile(dir / L"Enchanting_Room.blueprint");

        GenerateBeaconPyramid(4);
        s_current.SaveToFile(dir / L"Beacon_Pyramid_4Tier.blueprint");

        GenerateStarterHouse();
        s_current.SaveToFile(dir / L"Starter_House.blueprint");
    }

    // Default to nether portal template
    GenerateNetherPortal();
}

Schematic& Current() {
    return s_current;
}

std::vector<std::string> AvailableTemplates() {
    return {
        "Nether Portal (4x5)",
        "Enchanting Room (15 Bookshelves)",
        "Beacon Base (1 Tier 3x3)",
        "Beacon Base (4 Tiers 9x9)",
        "Starter House (7x7)",
        "Mob Farm Platform",
        "Circle Generator",
        "Sphere Generator",
        "Hollow Box Generator"
    };
}

std::vector<std::string> AvailableFiles() {
    std::vector<std::string> list;
    auto dir = SchematicDir();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext == ".blueprint" || ext == ".schem" || ext == ".txt") {
            list.push_back(entry.path().filename().string());
        }
    }
    std::sort(list.begin(), list.end());
    return list;
}

void LoadTemplate(const std::string& templateName) {
    if (templateName.find("Nether Portal") != std::string::npos) GenerateNetherPortal();
    else if (templateName.find("Enchanting") != std::string::npos) GenerateEnchantingRoom();
    else if (templateName.find("1 Tier") != std::string::npos) GenerateBeaconPyramid(1);
    else if (templateName.find("4 Tiers") != std::string::npos) GenerateBeaconPyramid(4);
    else if (templateName.find("Starter House") != std::string::npos) GenerateStarterHouse();
    else if (templateName.find("Mob Farm") != std::string::npos) GenerateMobFarm();
    else if (templateName.find("Circle") != std::string::npos) GenerateCircle(5, 1, "stone");
    else if (templateName.find("Sphere") != std::string::npos) GenerateSphere(4, "glass");
    else if (templateName.find("Hollow Box") != std::string::npos) GenerateHollowBox(7, 5, 7, "stone");
}

bool LoadFile(const std::string& fileName) {
    return s_current.LoadFromFile(SchematicDir() / fileName);
}

// ---------------------------------------------------------------- Generators

void GenerateNetherPortal() {
    s_current.Clear();
    s_current.name = "Nether Portal";
    // 4x5 standard portal
    for (int x = 0; x < 4; ++x) {
        s_current.blocks.push_back({x, 0, 0, "obsidian", BlockColorFromName("obsidian")});
        s_current.blocks.push_back({x, 4, 0, "obsidian", BlockColorFromName("obsidian")});
    }
    for (int y = 1; y < 4; ++y) {
        s_current.blocks.push_back({0, y, 0, "obsidian", BlockColorFromName("obsidian")});
        s_current.blocks.push_back({3, y, 0, "obsidian", BlockColorFromName("obsidian")});
        // Portal center
        s_current.blocks.push_back({1, y, 0, "portal", BlockColorFromName("portal")});
        s_current.blocks.push_back({2, y, 0, "portal", BlockColorFromName("portal")});
    }
    s_current.RecalculateBoundsAndMaterials();
}

void GenerateEnchantingRoom() {
    s_current.Clear();
    s_current.name = "Enchanting Room";
    // 5x5 bookshelf ring with opening at South side
    for (int y = 0; y < 2; ++y) {
        // North wall (z = 0)
        for (int x = 0; x < 5; ++x) s_current.blocks.push_back({x, y, 0, "bookshelf", BlockColorFromName("bookshelf")});
        // West & East walls
        for (int z = 1; z < 5; ++z) {
            s_current.blocks.push_back({0, y, z, "bookshelf", BlockColorFromName("bookshelf")});
            s_current.blocks.push_back({4, y, z, "bookshelf", BlockColorFromName("bookshelf")});
        }
        // South opening (doorway at center)
        s_current.blocks.push_back({1, y, 4, "bookshelf", BlockColorFromName("bookshelf")});
        s_current.blocks.push_back({3, y, 4, "bookshelf", BlockColorFromName("bookshelf")});
    }
    // Enchanting table at center
    s_current.blocks.push_back({2, 0, 2, "obsidian", BlockColorFromName("obsidian")});
    s_current.RecalculateBoundsAndMaterials();
}

void GenerateBeaconPyramid(int tiers) {
    s_current.Clear();
    s_current.name = "Beacon Pyramid (" + std::to_string(tiers) + " Tiers)";
    tiers = std::clamp(tiers, 1, 4);

    const int maxDim = 1 + (tiers * 2);
    int currentY = 0;
    for (int t = tiers; t >= 1; --t) {
        const int radius = t;
        const int center = tiers;
        for (int x = center - radius; x <= center + radius; ++x) {
            for (int z = center - radius; z <= center + radius; ++z) {
                s_current.blocks.push_back({x, currentY, z, "iron_block", BlockColorFromName("iron_block")});
            }
        }
        currentY++;
    }
    // Beacon on top
    s_current.blocks.push_back({tiers, currentY, tiers, "beacon", BlockColorFromName("beacon")});
    s_current.RecalculateBoundsAndMaterials();
}

void GenerateStarterHouse() {
    s_current.Clear();
    s_current.name = "Starter House";
    const int W = 7, D = 7, H = 4;

    // Floor
    for (int x = 0; x < W; ++x) {
        for (int z = 0; z < D; ++z) {
            s_current.blocks.push_back({x, 0, z, "cobblestone", BlockColorFromName("cobblestone")});
        }
    }
    // Walls
    for (int y = 1; y <= H; ++y) {
        for (int x = 0; x < W; ++x) {
            for (int z = 0; z < D; ++z) {
                if (x == 0 || x == W - 1 || z == 0 || z == D - 1) {
                    const bool corner = (x == 0 || x == W - 1) && (z == 0 || z == D - 1);
                    const bool door = (z == D - 1 && (x == 3) && (y == 1 || y == 2));
                    const bool window = ((x == 0 || x == W - 1) && (z == 3) && y == 2) ||
                                       (z == 0 && (x == 2 || x == 4) && y == 2);
                    if (door) continue;
                    if (window) {
                        s_current.blocks.push_back({x, y, z, "glass", BlockColorFromName("glass")});
                    } else if (corner) {
                        s_current.blocks.push_back({x, y, z, "oak_log", BlockColorFromName("wood")});
                    } else {
                        s_current.blocks.push_back({x, y, z, "oak_planks", BlockColorFromName("plank")});
                    }
                }
            }
        }
    }
    // Roof (stairs/pyramid)
    for (int step = 0; step < 4; ++step) {
        const int y = H + 1 + step;
        const int xMin = step, xMax = W - 1 - step;
        const int zMin = step, zMax = D - 1 - step;
        for (int x = xMin; x <= xMax; ++x) {
            for (int z = zMin; z <= zMax; ++z) {
                if (x == xMin || x == xMax || z == zMin || z == zMax) {
                    s_current.blocks.push_back({x, y, z, "oak_planks", BlockColorFromName("plank")});
                }
            }
        }
    }
    s_current.RecalculateBoundsAndMaterials();
}

void GenerateMobFarm() {
    s_current.Clear();
    s_current.name = "Mob Drop Tower Base";
    const int size = 8;
    // Central drop chute (2x2 drop hole in center)
    for (int x = 0; x < size; ++x) {
        for (int z = 0; z < size; ++z) {
            const bool isHole = (x >= 3 && x <= 4 && z >= 3 && z <= 4);
            if (!isHole) {
                s_current.blocks.push_back({x, 0, z, "cobblestone", BlockColorFromName("cobblestone")});
            }
        }
    }
    // Outer perimeter walls
    for (int y = 1; y <= 3; ++y) {
        for (int x = 0; x < size; ++x) {
            for (int z = 0; z < size; ++z) {
                if (x == 0 || x == size - 1 || z == 0 || z == size - 1) {
                    s_current.blocks.push_back({x, y, z, "cobblestone", BlockColorFromName("cobblestone")});
                }
            }
        }
    }
    s_current.RecalculateBoundsAndMaterials();
}

void GenerateCircle(int radius, int height, const std::string& block) {
    s_current.Clear();
    s_current.name = "Circle (R=" + std::to_string(radius) + ")";
    radius = std::clamp(radius, 1, 64);
    height = std::clamp(height, 1, 64);
    const ImU32 col = BlockColorFromName(block);

    const int diameter = radius * 2 + 1;
    for (int y = 0; y < height; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            for (int z = -radius; z <= radius; ++z) {
                const float dist = std::sqrt(static_cast<float>(x * x + z * z));
                if (dist >= radius - 0.6f && dist <= radius + 0.5f) {
                    s_current.blocks.push_back({x + radius, y, z + radius, block, col});
                }
            }
        }
    }
    s_current.RecalculateBoundsAndMaterials();
}

void GenerateHollowBox(int width, int height, int depth, const std::string& block) {
    s_current.Clear();
    s_current.name = "Box (" + std::to_string(width) + "x" + std::to_string(height) + "x" + std::to_string(depth) + ")";
    width = std::clamp(width, 1, 64);
    height = std::clamp(height, 1, 64);
    depth = std::clamp(depth, 1, 64);
    const ImU32 col = BlockColorFromName(block);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int z = 0; z < depth; ++z) {
                if (x == 0 || x == width - 1 || y == 0 || y == height - 1 || z == 0 || z == depth - 1) {
                    s_current.blocks.push_back({x, y, z, block, col});
                }
            }
        }
    }
    s_current.RecalculateBoundsAndMaterials();
}

void GenerateSphere(int radius, const std::string& block) {
    s_current.Clear();
    s_current.name = "Sphere (R=" + std::to_string(radius) + ")";
    radius = std::clamp(radius, 1, 32);
    const ImU32 col = BlockColorFromName(block);

    for (int x = -radius; x <= radius; ++x) {
        for (int y = -radius; y <= radius; ++y) {
            for (int z = -radius; z <= radius; ++z) {
                const float dist = std::sqrt(static_cast<float>(x * x + y * y + z * z));
                if (dist >= radius - 0.7f && dist <= radius + 0.4f) {
                    s_current.blocks.push_back({x + radius, y + radius, z + radius, block, col});
                }
            }
        }
    }
    s_current.RecalculateBoundsAndMaterials();
}

// ---------------------------------------------------------------- 3D Rendering

void Draw3D(ImDrawList* dl, const GameState& state, ImVec2 screen) {
    if (!state.valid || s_current.blocks.empty()) return;

    const Vec3 origin = s_current.origin;
    const Vec3 cam = state.origin;

    // Draw outer bounding volume outline if enabled
    if (s_current.showBoundingBox && s_current.sizeX > 0) {
        const ImU32 bboxColor = IM_COL32(255, 215, 0, 180); // Gold outline
        DrawVolumeOutline(dl, state, origin, s_current.sizeX, s_current.sizeY, s_current.sizeZ, bboxColor, 2.0f, screen);
    }

    const float maxDistSq = s_current.renderDistance * s_current.renderDistance;

    for (const auto& b : s_current.blocks) {
        // Layer isolation filtering
        if (!s_current.allLayers) {
            if (b.y < s_current.minLayer || b.y > s_current.maxLayer) continue;
        }

        const Vec3 worldPos{origin.x + static_cast<float>(b.x),
                            origin.y + static_cast<float>(b.y),
                            origin.z + static_cast<float>(b.z)};

        // Distance culling from camera
        const float dx = worldPos.x + 0.5f - cam.x;
        const float dy = worldPos.y + 0.5f - cam.y;
        const float dz = worldPos.z + 0.5f - cam.z;
        if (dx * dx + dy * dy + dz * dz > maxDistSq) continue;

        DrawBlockWireframe(dl, state, worldPos, b.color, 1.2f, screen);
    }
}

// ---------------------------------------------------------------- UI Tab

void DrawPage(const GameState& state) {
    if (ImGui::BeginTabBar("schem_tabs")) {
        if (ImGui::BeginTabItem("Control")) {
            ImGui::Spacing();
            ImGui::Text("Active Blueprint: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", s_current.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%d blocks, %dx%dx%d)", static_cast<int>(s_current.blocks.size()),
                                s_current.sizeX, s_current.sizeY, s_current.sizeZ);

            ImGui::Separator();

            // Placement Position
            int coords[3] = {static_cast<int>(s_current.origin.x), static_cast<int>(s_current.origin.y),
                             static_cast<int>(s_current.origin.z)};
            if (ImGui::InputInt3("Origin X Y Z", coords)) {
                s_current.origin.x = static_cast<float>(coords[0]);
                s_current.origin.y = static_cast<float>(coords[1]);
                s_current.origin.z = static_cast<float>(coords[2]);
            }

            ImGui::BeginDisabled(!state.valid);
            if (ImGui::Button("Place at My Position")) {
                const Vec3 feet = state.hasPlayer ? state.playerPos : state.origin;
                s_current.origin.x = std::floor(feet.x);
                s_current.origin.y = std::floor(feet.y - (state.hasPlayer ? kEyeHeight : 0.0f));
                s_current.origin.z = std::floor(feet.z);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Rotate 90° (Around Y)")) {
                s_current.Rotate90();
                ui::Notify("Schematic", "Rotated 90 degrees");
            }

            // Quick Nudge Buttons
            ImGui::Text("Nudge Position: ");
            ImGui::SameLine();
            if (ImGui::Button("-X")) s_current.origin.x -= 1;
            ImGui::SameLine();
            if (ImGui::Button("+X")) s_current.origin.x += 1;
            ImGui::SameLine();
            if (ImGui::Button("-Y")) s_current.origin.y -= 1;
            ImGui::SameLine();
            if (ImGui::Button("+Y")) s_current.origin.y += 1;
            ImGui::SameLine();
            if (ImGui::Button("-Z")) s_current.origin.z -= 1;
            ImGui::SameLine();
            if (ImGui::Button("+Z")) s_current.origin.z += 1;

            ImGui::SeparatorText("Layer Control (Litematica Style)");
            ImGui::Checkbox("Show all layers", &s_current.allLayers);
            if (!s_current.allLayers) {
                const int maxY = std::max(0, s_current.sizeY - 1);
                ImGui::SliderInt("Layer", &s_current.minLayer, 0, maxY, "Layer %d");
                s_current.maxLayer = s_current.minLayer;

                ImGui::SameLine();
                if (ImGui::SmallButton("- Layer") && s_current.minLayer > 0) {
                    s_current.minLayer--;
                    s_current.maxLayer = s_current.minLayer;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("+ Layer") && s_current.minLayer < maxY) {
                    s_current.minLayer++;
                    s_current.maxLayer = s_current.minLayer;
                }
            }

            ImGui::SeparatorText("Display Options");
            ImGui::Checkbox("Draw bounding box", &s_current.showBoundingBox);
            ImGui::SliderFloat("Render distance", &s_current.renderDistance, 16.0f, 128.0f, "%.0f blocks");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Blueprints & Templates")) {
            ImGui::Spacing();
            ImGui::Text("Choose a built-in template or loaded file:");

            // Templates
            static int selectedTemplate = 0;
            const auto templates = AvailableTemplates();
            if (ImGui::BeginCombo("Templates", templates[selectedTemplate].c_str())) {
                for (int i = 0; i < static_cast<int>(templates.size()); ++i) {
                    if (ImGui::Selectable(templates[i].c_str(), selectedTemplate == i)) {
                        selectedTemplate = i;
                        LoadTemplate(templates[i]);
                        ui::Notify("Loaded Blueprint", templates[i]);
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SeparatorText("Custom Files from disk");
            const auto files = AvailableFiles();
            if (files.empty()) {
                ImGui::TextDisabled("No custom files found in:");
                ImGui::TextDisabled("%%LOCALAPPDATA%%\\BedrockWaypoints\\schematics\\");
            } else {
                static int selectedFile = 0;
                if (selectedFile >= static_cast<int>(files.size())) selectedFile = 0;
                if (ImGui::BeginCombo("File##list", files[selectedFile].c_str())) {
                    for (int i = 0; i < static_cast<int>(files.size()); ++i) {
                        if (ImGui::Selectable(files[i].c_str(), selectedFile == i)) {
                            selectedFile = i;
                            if (LoadFile(files[i])) ui::Notify("Loaded File", files[i]);
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::Spacing();
            if (ImGui::Button("Open Schematics Folder in Explorer")) {
                ShellExecuteW(nullptr, L"open", SchematicDir().c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Shape Generator")) {
            ImGui::Spacing();
            ImGui::Text("Instantly generate mathematical blueprints:");

            static int shapeType = 0; // 0 = Circle, 1 = Hollow Box, 2 = Sphere
            const char* shapes[] = {"Circle / Cylinder", "Hollow Box / Walls", "Sphere / Dome"};
            ImGui::Combo("Shape", &shapeType, shapes, IM_ARRAYSIZE(shapes));

            static int shapeRadius = 5;
            static int shapeHeight = 3;
            static int boxW = 7, boxH = 5, boxD = 7;
            static char blockName[32] = "stone";

            ImGui::InputText("Block type", blockName, sizeof(blockName));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("e.g. stone, glass, obsidian, iron_block, oak_planks");

            if (shapeType == 0) { // Circle
                ImGui::SliderInt("Radius", &shapeRadius, 2, 40);
                ImGui::SliderInt("Height", &shapeHeight, 1, 30);
                if (ImGui::Button("Generate Circle Blueprint")) {
                    GenerateCircle(shapeRadius, shapeHeight, blockName);
                    ui::Notify("Generated", "Circle blueprint ready");
                }
            } else if (shapeType == 1) { // Box
                ImGui::SliderInt("Width", &boxW, 3, 50);
                ImGui::SliderInt("Height", &boxH, 2, 40);
                ImGui::SliderInt("Depth", &boxD, 3, 50);
                if (ImGui::Button("Generate Box Blueprint")) {
                    GenerateHollowBox(boxW, boxH, boxD, blockName);
                    ui::Notify("Generated", "Box blueprint ready");
                }
            } else if (shapeType == 2) { // Sphere
                ImGui::SliderInt("Radius", &shapeRadius, 2, 25);
                if (ImGui::Button("Generate Sphere Blueprint")) {
                    GenerateSphere(shapeRadius, blockName);
                    ui::Notify("Generated", "Sphere blueprint ready");
                }
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Materials List")) {
            ImGui::Spacing();
            ImGui::Text("Materials required for: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", s_current.name.c_str());

            if (s_current.materials.empty()) {
                ImGui::TextDisabled("No materials needed.");
            } else {
                if (ImGui::BeginTable("materials_table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {
                    ImGui::TableSetupColumn("Block Type");
                    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Stacks", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableHeadersRow();

                    for (const auto& [matName, count] : s_current.materials) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(matName.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", count);
                        ImGui::TableNextColumn();
                        const int stacks = count / 64;
                        const int rem = count % 64;
                        if (stacks > 0) ImGui::Text("%d x64 + %d", stacks, rem);
                        else ImGui::Text("%d", rem);
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

} // namespace schematic
