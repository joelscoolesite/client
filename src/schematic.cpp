#include "schematic.h"

#include "config.h"
#include "ui.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>

SchematicPlacement schematic::g_placement;
SchematicView schematic::g_view;

namespace {

// A schematic block at its final world position. `faces` has a bit for every side without a neighbour
// (0 -X, 1 +X, 2 -Y, 3 +Y, 4 -Z, 5 +Z).
struct WorldBlock {
    int x, y, z;
    uint16_t state;
    uint8_t faces;
};

// Blocks are sorted into 16x16x16 sections so far away ones are skipped cheaply.
struct Section {
    int cx, cy, cz;
    uint32_t begin, end;
};

std::unique_ptr<Schematic> s_current;
std::vector<WorldBlock> s_blocks;
std::vector<Section> s_sections;
std::vector<ImU32> s_colors; // per state

std::mutex s_loadMutex;
std::unique_ptr<Schematic> s_pending;
std::string s_pendingFile;
std::string s_error;
std::atomic<bool> s_loading{false};

std::filesystem::path SettingsFile() {
    return DataDir() / L"schematic.ini";
}

// Colour from the block id: the dye colour for wool/concrete/glass and friends, otherwise the material.
ImU32 BlockColor(const std::string& fullId) {
    const std::string id = fullId.substr(fullId.find(':') + 1);
    struct Rule {
        const char* key;
        ImU32 color;
    };
    static const Rule dyes[] = {
        {"light_blue", IM_COL32(90, 170, 230, 255)}, {"light_gray", IM_COL32(160, 160, 155, 255)},
        {"white", IM_COL32(235, 235, 235, 255)},     {"orange", IM_COL32(240, 120, 30, 255)},
        {"magenta", IM_COL32(190, 70, 180, 255)},    {"yellow", IM_COL32(250, 210, 50, 255)},
        {"lime", IM_COL32(120, 200, 40, 255)},       {"pink", IM_COL32(240, 140, 170, 255)},
        {"gray", IM_COL32(70, 75, 80, 255)},         {"cyan", IM_COL32(20, 140, 150, 255)},
        {"purple", IM_COL32(120, 45, 170, 255)},     {"blue", IM_COL32(50, 60, 160, 255)},
        {"brown", IM_COL32(110, 70, 40, 255)},       {"green", IM_COL32(80, 110, 30, 255)},
        {"red", IM_COL32(160, 40, 35, 255)},         {"black", IM_COL32(25, 25, 30, 255)},
    };
    static const Rule materials[] = {
        {"glass", IM_COL32(170, 220, 240, 255)},    {"water", IM_COL32(50, 100, 230, 255)},
        {"lava", IM_COL32(240, 110, 20, 255)},      {"leaves", IM_COL32(60, 140, 40, 255)},
        {"grass", IM_COL32(90, 160, 60, 255)},      {"moss", IM_COL32(90, 140, 50, 255)},
        {"dirt", IM_COL32(130, 90, 60, 255)},       {"mud", IM_COL32(80, 65, 60, 255)},
        {"sandstone", IM_COL32(215, 200, 140, 255)}, {"sand", IM_COL32(220, 205, 150, 255)},
        {"gravel", IM_COL32(135, 125, 120, 255)},   {"dark_oak", IM_COL32(70, 45, 25, 255)},
        {"oak", IM_COL32(170, 135, 80, 255)},       {"spruce", IM_COL32(115, 85, 50, 255)},
        {"birch", IM_COL32(200, 185, 125, 255)},    {"jungle", IM_COL32(165, 115, 80, 255)},
        {"acacia", IM_COL32(170, 90, 50, 255)},
        {"mangrove", IM_COL32(120, 50, 45, 255)},   {"cherry", IM_COL32(225, 175, 165, 255)},
        {"bamboo", IM_COL32(195, 175, 85, 255)},    {"crimson", IM_COL32(125, 55, 80, 255)},
        {"warped", IM_COL32(45, 110, 110, 255)},    {"log", IM_COL32(110, 85, 50, 255)},
        {"plank", IM_COL32(170, 135, 80, 255)},     {"wood", IM_COL32(110, 85, 50, 255)},
        {"brick", IM_COL32(150, 85, 70, 255)},      {"quartz", IM_COL32(235, 230, 225, 255)},
        {"gold", IM_COL32(245, 205, 60, 255)},      {"iron", IM_COL32(215, 215, 215, 255)},
        {"diamond", IM_COL32(100, 220, 215, 255)},  {"emerald", IM_COL32(50, 200, 100, 255)},
        {"redstone", IM_COL32(200, 30, 20, 255)},   {"lapis", IM_COL32(40, 70, 170, 255)},
        {"copper", IM_COL32(195, 110, 80, 255)},    {"obsidian", IM_COL32(30, 20, 45, 255)},
        {"netherrack", IM_COL32(115, 45, 45, 255)}, {"end_stone", IM_COL32(220, 220, 160, 255)},
        {"purpur", IM_COL32(170, 125, 170, 255)},   {"prismarine", IM_COL32(90, 165, 150, 255)},
        {"snow", IM_COL32(245, 250, 250, 255)},     {"ice", IM_COL32(150, 185, 245, 255)},
        {"deepslate", IM_COL32(80, 80, 85, 255)},   {"blackstone", IM_COL32(45, 40, 45, 255)},
        {"terracotta", IM_COL32(150, 95, 70, 255)},
        {"stone", IM_COL32(125, 125, 125, 255)},    {"cobble", IM_COL32(115, 115, 115, 255)},
        {"andesite", IM_COL32(135, 135, 135, 255)}, {"diorite", IM_COL32(190, 190, 190, 255)},
        {"granite", IM_COL32(150, 105, 85, 255)},   {"tuff", IM_COL32(105, 105, 95, 255)},
    };
    const bool dyed = id.find("wool") != std::string::npos || id.find("concrete") != std::string::npos ||
                      id.find("glass") != std::string::npos || id.find("terracotta") != std::string::npos ||
                      id.find("carpet") != std::string::npos || id.find("bed") != std::string::npos;
    if (dyed)
        for (const Rule& r : dyes)
            if (id.find(r.key) != std::string::npos) return r.color;
    for (const Rule& r : materials)
        if (id.find(r.key) != std::string::npos) return r.color;
    // Anything else gets a stable colour from its name.
    const size_t h = std::hash<std::string>{}(id);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.45f + (h & 0xFF) / 600.0f, 0.45f + ((h >> 8) & 0xFF) / 600.0f, 0.45f + ((h >> 16) & 0xFF) / 600.0f, 1));
}

void Rebuild() {
    s_blocks.clear();
    s_sections.clear();
    if (!s_current) return;
    const Schematic& sc = *s_current;
    const auto& p = schematic::g_placement;
    int size[3];
    schematic::RotatedSize(size);
    const int rx = size[0], rz = size[2];

    s_blocks.reserve(sc.blocks.size());
    for (const SchematicBlock& b : sc.blocks) {
        int x = p.mirror ? sc.sizeX - 1 - b.x : b.x, z = b.z;
        switch (p.rotation & 3) {
        case 1: std::tie(x, z) = std::make_pair(sc.sizeZ - 1 - z, x); break;
        case 2: std::tie(x, z) = std::make_pair(sc.sizeX - 1 - x, sc.sizeZ - 1 - z); break;
        case 3: std::tie(x, z) = std::make_pair(z, sc.sizeX - 1 - x); break;
        default: break;
        }
        s_blocks.push_back({x, b.y, z, b.state, 0});
    }

    // Faces touching another schematic block are never visible.
    const uint64_t volume = static_cast<uint64_t>(rx) * sc.sizeY * rz;
    const bool cull = volume <= (1ull << 30);
    std::vector<uint64_t> filled(cull ? (volume + 63) / 64 : 0);
    auto index = [&](int x, int y, int z) { return (static_cast<uint64_t>(y) * rz + z) * rx + x; };
    auto has = [&](int x, int y, int z) {
        if (x < 0 || y < 0 || z < 0 || x >= rx || y >= sc.sizeY || z >= rz) return false;
        const uint64_t i = index(x, y, z);
        return ((filled[i >> 6] >> (i & 63)) & 1) != 0;
    };
    if (cull)
        for (const WorldBlock& b : s_blocks) filled[index(b.x, b.y, b.z) >> 6] |= 1ull << (index(b.x, b.y, b.z) & 63);
    for (WorldBlock& b : s_blocks) {
        if (!cull) {
            b.faces = 0x3F;
        } else {
            b.faces = (!has(b.x - 1, b.y, b.z) << 0) | (!has(b.x + 1, b.y, b.z) << 1) | (!has(b.x, b.y - 1, b.z) << 2) |
                      (!has(b.x, b.y + 1, b.z) << 3) | (!has(b.x, b.y, b.z - 1) << 4) | (!has(b.x, b.y, b.z + 1) << 5);
        }
        b.x += p.origin[0];
        b.y += p.origin[1];
        b.z += p.origin[2];
    }

    auto key = [](const WorldBlock& b) { return std::make_tuple(b.x >> 4, b.y >> 4, b.z >> 4); };
    std::sort(s_blocks.begin(), s_blocks.end(), [&](const WorldBlock& a, const WorldBlock& b) { return key(a) < key(b); });
    for (uint32_t i = 0; i < s_blocks.size(); ++i) {
        const auto [cx, cy, cz] = key(s_blocks[i]);
        if (s_sections.empty() || s_sections.back().cx != cx || s_sections.back().cy != cy || s_sections.back().cz != cz)
            s_sections.push_back({cx, cy, cz, i, i});
        s_sections.back().end = i + 1;
    }
}

// Takes a schematic the loader thread finished.
void TakePending() {
    if (s_loading) return;
    std::unique_ptr<Schematic> loaded;
    {
        std::lock_guard lock(s_loadMutex);
        if (!s_pending) return;
        loaded = std::move(s_pending);
    }
    s_current = std::move(loaded);
    s_colors.clear();
    for (const std::string& id : s_current->states) s_colors.push_back(BlockColor(id));
    Rebuild();
}

ImU32 Shade(ImU32 color, float light, float alpha) {
    const auto channel = [&](int shift) { return static_cast<ImU32>(((color >> shift) & 0xFF) * light) << shift; };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT) |
           (static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * 255) << IM_COL32_A_SHIFT);
}

struct Clip {
    float x, y, z, w;
};

Clip ToClip(const GameState& s, float x, float y, float z) {
    const float rel[4] = {x - s.origin.x, y - s.origin.y, z - s.origin.z, 1.0f};
    float v[4], c[4];
    for (int r = 0; r < 4; ++r) {
        v[r] = 0;
        for (int k = 0; k < 4; ++k) v[r] += s.view[k * 4 + r] * rel[k];
    }
    for (int r = 0; r < 4; ++r) {
        c[r] = 0;
        for (int k = 0; k < 4; ++k) c[r] += s.proj[k * 4 + r] * v[k];
    }
    return {c[0], c[1], c[2], c[3]};
}

// Draws one block face (a world-space quad), cut at the near plane.
void DrawFace(ImDrawList* dl, const GameState& s, const float (&corners)[4][3], ImU32 fill, ImU32 line,
              ImVec2 screen) {
    Clip in[4];
    int outside[4] = {0, 0, 0, 0}; // left, right, bottom, top
    for (int i = 0; i < 4; ++i) {
        in[i] = ToClip(s, corners[i][0], corners[i][1], corners[i][2]);
        outside[0] += in[i].x < -in[i].w, outside[1] += in[i].x > in[i].w;
        outside[2] += in[i].y < -in[i].w, outside[3] += in[i].y > in[i].w;
    }
    for (int side : outside)
        if (side == 4) return; // off screen

    constexpr float kNear = 0.05f;
    Clip poly[8];
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        const Clip& a = in[i];
        const Clip& b = in[(i + 1) % 4];
        if (a.w >= kNear) poly[n++] = a;
        if ((a.w >= kNear) != (b.w >= kNear)) {
            const float t = (kNear - a.w) / (b.w - a.w);
            poly[n++] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, kNear};
        }
    }
    if (n < 3) return;
    ImVec2 points[8];
    for (int i = 0; i < n; ++i)
        points[i] = ImVec2((poly[i].x / poly[i].w + 1) * 0.5f * screen.x, (1 - poly[i].y / poly[i].w) * 0.5f * screen.y);
    if (fill & IM_COL32_A_MASK) dl->AddConvexPolyFilled(points, n, fill);
    if (line & IM_COL32_A_MASK) dl->AddPolyline(points, n, line, ImDrawFlags_Closed, 1.0f);
}

bool SameWorld(const GameState& s) {
    const auto& p = schematic::g_placement;
    if (!p.world.empty() && s.world[0] && p.world != s.world) return false;
    if (!p.dimension.empty() && s.dimension[0] && p.dimension != s.dimension) return false;
    return true;
}

bool LayerVisible(int layer, int active) {
    switch (schematic::g_view.layerMode) {
    case LayerMode::Single: return layer == active;
    case LayerMode::Below: return layer <= active;
    default: return true;
    }
}

} // namespace

namespace schematic {

std::filesystem::path Folder() {
    auto dir = DataDir() / L"schematics";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::vector<std::string> Files() {
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(Folder(), ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".litematic") continue;
        const std::u8string name = entry.path().filename().u8string(); // UTF-8, so ImGui shows any name
        files.emplace_back(name.begin(), name.end());
    }
    std::sort(files.begin(), files.end());
    return files;
}

void Init() {
    std::ifstream f(SettingsFile());
    std::string line;
    auto& p = g_placement;
    auto& v = g_view;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        try {
            if (key == "file") p.file = value;
            else if (key == "x") p.origin[0] = std::stoi(value);
            else if (key == "y") p.origin[1] = std::stoi(value);
            else if (key == "z") p.origin[2] = std::stoi(value);
            else if (key == "rotation") p.rotation = std::stoi(value) & 3;
            else if (key == "mirror") p.mirror = value == "1";
            else if (key == "visible") p.visible = value == "1";
            else if (key == "world") p.world = value;
            else if (key == "dimension") p.dimension = value;
            else if (key == "renderDistance") v.renderDistance = std::clamp(std::stof(value), 4.0f, 128.0f);
            else if (key == "opacity") v.opacity = std::clamp(std::stof(value), 0.0f, 1.0f);
            else if (key == "outlines") v.outlines = value == "1";
            else if (key == "layerMode") v.layerMode = static_cast<LayerMode>(std::clamp(std::stoi(value), 0, 2));
            else if (key == "layer") v.layer = std::max(0, std::stoi(value));
            else if (key == "followPlayer") v.followPlayer = value == "1";
        } catch (...) {
            // Ignore broken values.
        }
    }
    if (!p.file.empty()) Load(p.file);
}

void Save() {
    const auto& p = g_placement;
    const auto& v = g_view;
    std::ofstream f(SettingsFile(), std::ios::trunc);
    f << "file=" << p.file << "\nx=" << p.origin[0] << "\ny=" << p.origin[1] << "\nz=" << p.origin[2]
      << "\nrotation=" << p.rotation << "\nmirror=" << p.mirror << "\nvisible=" << p.visible << "\nworld=" << p.world
      << "\ndimension=" << p.dimension << "\nrenderDistance=" << v.renderDistance << "\nopacity=" << v.opacity
      << "\noutlines=" << v.outlines << "\nlayerMode=" << static_cast<int>(v.layerMode) << "\nlayer=" << v.layer
      << "\nfollowPlayer=" << v.followPlayer << "\n";
}

void Load(const std::string& file) {
    if (s_loading.exchange(true)) return;
    {
        std::lock_guard lock(s_loadMutex);
        s_error.clear();
        s_pendingFile = file;
    }
    // Big schematics take a moment to decompress, so the game keeps running meanwhile.
    std::thread([file] {
        auto loaded = std::make_unique<Schematic>();
        std::string error;
        const bool ok = LoadLitematic(Folder() / std::u8string(file.begin(), file.end()), *loaded, error);
        {
            std::lock_guard lock(s_loadMutex);
            if (ok) s_pending = std::move(loaded);
            else s_error = error;
        }
        Log("Schematic " + file + (ok ? ": loaded" : ": " + error));
        s_loading = false;
    }).detach();
}

void Unload() {
    s_current.reset();
    s_blocks.clear();
    s_sections.clear();
    g_placement.file.clear();
    Save();
}

bool Loading() {
    return s_loading;
}

std::string LastError() {
    std::lock_guard lock(s_loadMutex);
    return s_error;
}

const Schematic* Current() {
    TakePending();
    return s_current.get();
}

void PlacementChanged() {
    g_placement.rotation &= 3;
    Rebuild();
    Save();
}

void RotatedSize(int out[3]) {
    if (!s_current) {
        out[0] = out[1] = out[2] = 0;
        return;
    }
    const bool turned = g_placement.rotation & 1;
    out[0] = turned ? s_current->sizeZ : s_current->sizeX;
    out[1] = s_current->sizeY;
    out[2] = turned ? s_current->sizeX : s_current->sizeZ;
}

int ActiveLayer(const GameState& state) {
    const int height = s_current ? s_current->sizeY : 1;
    int layer = g_view.layer;
    if (g_view.followPlayer && state.valid) {
        const Vec3 eye = state.hasPlayer ? state.playerPos : state.origin;
        layer = static_cast<int>(std::floor(eye.y - kEyeHeight + 0.01f)) - g_placement.origin[1];
    }
    return std::clamp(layer, 0, std::max(0, height - 1));
}

std::vector<std::pair<std::string, int>> Materials(const GameState& state, bool visibleOnly) {
    std::vector<std::pair<std::string, int>> list;
    if (!s_current) return list;
    std::vector<int> counts(s_current->states.size());
    const int active = ActiveLayer(state);
    for (const WorldBlock& b : s_blocks)
        if (!visibleOnly || LayerVisible(b.y - g_placement.origin[1], active)) ++counts[b.state];
    for (size_t i = 0; i < counts.size(); ++i)
        if (counts[i]) list.push_back({s_current->states[i], counts[i]});
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    return list;
}

void DrawWorld(ImDrawList* dl, const GameState& s, ImVec2 screen) {
    TakePending();
    if (!s_current || !g_placement.visible || !s.valid || !SameWorld(s)) return;

    const float range = g_view.renderDistance;
    const Vec3 cam = s.origin;
    const int active = ActiveLayer(s);
    const int baseY = g_placement.origin[1];
    const float alpha = g_view.opacity;
    constexpr size_t kMaxFaces = 12000;

    // Nearest sections first, so the face limit drops the far ones.
    std::vector<std::pair<float, const Section*>> near;
    for (const Section& sec : s_sections) {
        const float dx = std::max({sec.cx * 16.0f - cam.x, 0.0f, cam.x - (sec.cx * 16.0f + 16)});
        const float dy = std::max({sec.cy * 16.0f - cam.y, 0.0f, cam.y - (sec.cy * 16.0f + 16)});
        const float dz = std::max({sec.cz * 16.0f - cam.z, 0.0f, cam.z - (sec.cz * 16.0f + 16)});
        const float d = dx * dx + dy * dy + dz * dz;
        if (d <= range * range) near.push_back({d, &sec});
    }
    std::sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    // Light per face direction so the shape reads as 3D.
    static const float kLight[6] = {0.75f, 0.75f, 0.55f, 1.0f, 0.85f, 0.85f};
    size_t drawn = 0;
    for (const auto& [dist, sec] : near) {
        for (uint32_t i = sec->begin; i < sec->end && drawn < kMaxFaces; ++i) {
            const WorldBlock& b = s_blocks[i];
            const int layer = b.y - baseY;
            if (!LayerVisible(layer, active)) continue;
            const float fx = static_cast<float>(b.x), fy = static_cast<float>(b.y), fz = static_cast<float>(b.z);
            const float ddx = fx + 0.5f - cam.x, ddy = fy + 0.5f - cam.y, ddz = fz + 0.5f - cam.z;
            if (ddx * ddx + ddy * ddy + ddz * ddz > range * range) continue;

            uint8_t faces = b.faces;
            // The top of the highest visible layer is always open, even if the schematic continues above.
            if (g_view.layerMode != LayerMode::All && layer == active) faces |= 1 << 3;
            if (g_view.layerMode == LayerMode::Single) faces |= 1 << 2;
            const ImU32 base = s_colors[b.state];
            const float x0 = fx, x1 = fx + 1, y0 = fy, y1 = fy + 1, z0 = fz, z1 = fz + 1;
            for (int f = 0; f < 6; ++f) {
                if (!(faces & (1 << f))) continue;
                // Only faces turned towards the camera.
                const bool towards = f == 0 ? cam.x < x0 : f == 1 ? cam.x > x1 : f == 2 ? cam.y < y0
                                   : f == 3 ? cam.y > y1 : f == 4 ? cam.z < z0 : cam.z > z1;
                if (!towards) continue;
                float c[4][3];
                switch (f) {
                case 0: c[0][0] = x0, c[0][1] = y0, c[0][2] = z0, c[1][0] = x0, c[1][1] = y1, c[1][2] = z0,
                        c[2][0] = x0, c[2][1] = y1, c[2][2] = z1, c[3][0] = x0, c[3][1] = y0, c[3][2] = z1; break;
                case 1: c[0][0] = x1, c[0][1] = y0, c[0][2] = z0, c[1][0] = x1, c[1][1] = y0, c[1][2] = z1,
                        c[2][0] = x1, c[2][1] = y1, c[2][2] = z1, c[3][0] = x1, c[3][1] = y1, c[3][2] = z0; break;
                case 2: c[0][0] = x0, c[0][1] = y0, c[0][2] = z0, c[1][0] = x0, c[1][1] = y0, c[1][2] = z1,
                        c[2][0] = x1, c[2][1] = y0, c[2][2] = z1, c[3][0] = x1, c[3][1] = y0, c[3][2] = z0; break;
                case 3: c[0][0] = x0, c[0][1] = y1, c[0][2] = z0, c[1][0] = x1, c[1][1] = y1, c[1][2] = z0,
                        c[2][0] = x1, c[2][1] = y1, c[2][2] = z1, c[3][0] = x0, c[3][1] = y1, c[3][2] = z1; break;
                case 4: c[0][0] = x0, c[0][1] = y0, c[0][2] = z0, c[1][0] = x1, c[1][1] = y0, c[1][2] = z0,
                        c[2][0] = x1, c[2][1] = y1, c[2][2] = z0, c[3][0] = x0, c[3][1] = y1, c[3][2] = z0; break;
                default: c[0][0] = x0, c[0][1] = y0, c[0][2] = z1, c[1][0] = x0, c[1][1] = y1, c[1][2] = z1,
                        c[2][0] = x1, c[2][1] = y1, c[2][2] = z1, c[3][0] = x1, c[3][1] = y0, c[3][2] = z1; break;
                }
                const ImU32 fill = Shade(base, kLight[f], alpha);
                const ImU32 line = g_view.outlines ? Shade(base, kLight[f] * 0.6f, std::min(1.0f, alpha * 2 + 0.2f)) : 0;
                DrawFace(dl, s, c, fill, line, screen);
                ++drawn;
            }
        }
        if (drawn >= kMaxFaces) break;
    }
}

} // namespace schematic
