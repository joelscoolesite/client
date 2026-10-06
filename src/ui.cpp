#include "ui.h"

#include "config.h"
#include "game.h"
#include "waypoints.h"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

bool s_menuOpen = false;
bool s_menuKeyWasDown = false;
bool s_addKeyWasDown = false;

std::mutex s_statusMutex;
std::string s_status;

std::string s_toast;
ULONGLONG s_toastUntil = 0;

// "Add" tab
char s_newName[64] = "Home";
int s_newCoords[3] = {0, 64, 0};
float s_newColor[3] = {0.2f, 0.8f, 1.0f};
int s_newDimension = 0; // index into kDimensionChoices
bool s_newAlways = false;

// "Waypoints" tab editor
int s_editIndex = -1;
Waypoint s_edit;
char s_editName[64] = "";
int s_editCoords[3] = {0, 0, 0};
int s_editDimension = 0;

// Combo entries; the id is what gets stored in Waypoint::dimension.
struct DimensionChoice {
    const char* label;
    const char* id;
};
constexpr DimensionChoice kDimensionChoices[] = {
    {"Current", nullptr}, {"Any", ""}, {"Overworld", "Overworld"}, {"Nether", "Nether"}, {"The End", "TheEnd"},
};

constexpr float kPalette[][3] = {
    {0.2f, 0.8f, 1.0f}, {1.0f, 0.8f, 0.2f}, {0.4f, 1.0f, 0.4f},
    {1.0f, 0.4f, 0.9f}, {1.0f, 0.5f, 0.2f}, {0.6f, 0.6f, 1.0f},
};

void Toast(const std::string& text) {
    s_toast = text;
    s_toastUntil = GetTickCount64() + 2500;
}

float Distance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Vec3 FeetBlock(const GameState& state) {
    const Vec3 eye = state.hasPlayer ? state.playerPos : state.origin;
    return {std::floor(eye.x), std::floor(eye.y - kEyeHeight), std::floor(eye.z)};
}

int DimensionIndex(const std::string& id) {
    for (int i = 1; i < IM_ARRAYSIZE(kDimensionChoices); ++i)
        if (id == kDimensionChoices[i].id) return i;
    return 1; // Any
}

// Where to draw a waypoint right now, or false if it belongs to another world or dimension.
bool DisplayPosition(const Waypoint& w, const GameState& state, Vec3& out, bool& converted) {
    converted = false;
    if (!w.always && !w.world.empty() && state.world[0] && w.world != state.world) return false;

    out = w.pos;
    const std::string current = state.dimension;
    if (w.dimension.empty() || current.empty() || w.dimension == current) return true;
    if (!g_settings.netherConversion) return false;

    if (w.dimension == "Overworld" && current == "Nether") {
        out = {std::floor(w.pos.x / 8), w.pos.y, std::floor(w.pos.z / 8)};
    } else if (w.dimension == "Nether" && current == "Overworld") {
        out = {w.pos.x * 8, w.pos.y, w.pos.z * 8};
    } else {
        return false;
    }
    converted = true;
    return true;
}

Waypoint NewWaypointHere(const GameState& state, const std::string& name) {
    Waypoint w;
    w.name = name;
    w.pos = FeetBlock(state);
    w.world = state.world;
    w.dimension = state.dimension;
    return w;
}

void AddWaypointAtPlayer(const GameState& state) {
    if (!state.valid) return;
    char name[32];
    snprintf(name, sizeof(name), "Waypoint %d", static_cast<int>(g_waypoints.size()) + 1);
    Waypoint w = NewWaypointHere(state, name);
    std::copy(std::begin(kPalette[g_waypoints.size() % IM_ARRAYSIZE(kPalette)]),
              std::end(kPalette[g_waypoints.size() % IM_ARRAYSIZE(kPalette)]), w.color);
    g_waypoints.push_back(w);
    SaveWaypoints();
    Toast("Added " + w.name);
}

void HandleDeath() {
    DeathInfo death;
    if (!game::PollDeath(death) || !g_settings.deathWaypoints) return;

    Waypoint w;
    w.name = "Death";
    w.pos = {std::floor(death.feetPos.x), std::floor(death.feetPos.y), std::floor(death.feetPos.z)};
    w.color[0] = 1.0f;
    w.color[1] = 0.25f;
    w.color[2] = 0.25f;
    w.world = death.world;
    w.dimension = death.dimension;
    w.death = true;
    g_waypoints.push_back(w);

    // Keep only the newest death waypoints.
    int deaths = 0;
    for (auto it = g_waypoints.rbegin(); it != g_waypoints.rend(); ++it)
        if (it->death) ++deaths;
    for (auto it = g_waypoints.begin(); it != g_waypoints.end() && deaths > g_settings.maxDeathWaypoints;) {
        if (it->death) {
            it = g_waypoints.erase(it);
            --deaths;
        } else {
            ++it;
        }
    }
    s_editIndex = -1;
    SaveWaypoints();
    Toast("Death waypoint saved");
}

void PollKeys(const GameState& state) {
    const HWND gameWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
    const bool focused = GetForegroundWindow() == gameWindow;

    const bool menuDown = (GetAsyncKeyState(g_config.menuKey) & 0x8000) != 0;
    if (menuDown && !s_menuKeyWasDown && focused) s_menuOpen = !s_menuOpen;
    s_menuKeyWasDown = menuDown;

    const bool addDown = (GetAsyncKeyState(g_config.addWaypointKey) & 0x8000) != 0;
    if (addDown && !s_addKeyWasDown && focused && !ImGui::GetIO().WantTextInput) AddWaypointAtPlayer(state);
    s_addKeyWasDown = addDown;
}

// ---------------------------------------------------------------- world overlay

void DrawLabel(ImDrawList* dl, ImVec2 anchor, const char* text, ImU32 color) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 min(anchor.x - size.x / 2 - 5, anchor.y - size.y - 6);
    const ImVec2 max(anchor.x + size.x / 2 + 5, anchor.y);
    dl->AddRectFilled(min, max, IM_COL32(0, 0, 0, 150), 4.0f);
    dl->AddRectFilled(ImVec2(min.x, max.y - 2), max, color, 0.0f);
    dl->AddText(ImVec2(min.x + 5, min.y + 2), IM_COL32_WHITE, text);
}

void DrawWaypoint(ImDrawList* dl, const GameState& state, const Waypoint& w, float screenW, float screenH) {
    Vec3 pos;
    bool converted = false;
    if (!DisplayPosition(w, state, pos, converted)) return;

    // Waypoints are block coordinates; aim at the middle of the block.
    const Vec3 center{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(w.color[0], w.color[1], w.color[2], 1.0f));

    char label[160];
    const char* suffix = converted ? (std::strcmp(state.dimension, "Nether") == 0 ? " (/8)" : " (x8)") : "";
    if (g_settings.showDistance) {
        snprintf(label, sizeof(label), "%s%s  %.0fm", w.name.c_str(), suffix, Distance(center, state.origin));
    } else {
        snprintf(label, sizeof(label), "%s%s", w.name.c_str(), suffix);
    }

    float sx, sy;
    const bool onScreen = game::WorldToScreen(state, center, screenW, screenH, sx, sy) && sx >= 0 && sx <= screenW &&
                          sy >= 0 && sy <= screenH;

    if (onScreen) {
        if (g_settings.showBeam) {
            float bx, by, tx, ty;
            const Vec3 bottom{center.x, pos.y, center.z};
            const Vec3 top{center.x, pos.y + 100.0f, center.z};
            if (game::WorldToScreen(state, bottom, screenW, screenH, bx, by) &&
                game::WorldToScreen(state, top, screenW, screenH, tx, ty)) {
                const ImU32 beamColor =
                    ImGui::ColorConvertFloat4ToU32(ImVec4(w.color[0], w.color[1], w.color[2], 0.45f));
                dl->AddLine(ImVec2(bx, by), ImVec2(tx, ty), beamColor, 3.0f);
            }
        }
        dl->AddCircleFilled(ImVec2(sx, sy), 6.0f, color);
        dl->AddCircle(ImVec2(sx, sy), 6.0f, IM_COL32(0, 0, 0, 200), 0, 2.0f);
        DrawLabel(dl, ImVec2(sx, sy - 10), label, color);
        return;
    }

    // Off screen or behind us: put an arrow on the screen edge pointing towards it.
    const Vec3 v = game::ToViewSpace(state, center);
    float dx = v.x, dy = -v.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) {
        dx = 0;
        dy = 1;
    } else {
        dx /= len;
        dy /= len;
    }
    const float margin = 60.0f;
    const float hx = screenW / 2 - margin, hy = screenH / 2 - margin;
    const float t = std::min(std::fabs(dx) > 1e-4f ? hx / std::fabs(dx) : 1e9f,
                             std::fabs(dy) > 1e-4f ? hy / std::fabs(dy) : 1e9f);
    const ImVec2 p(screenW / 2 + dx * t, screenH / 2 + dy * t);

    const float size = 12.0f;
    const ImVec2 tip(p.x + dx * size, p.y + dy * size);
    const ImVec2 left(p.x - dy * size * 0.7f, p.y + dx * size * 0.7f);
    const ImVec2 right(p.x + dy * size * 0.7f, p.y - dx * size * 0.7f);
    dl->AddTriangleFilled(tip, left, right, color);
    dl->AddTriangle(tip, left, right, IM_COL32(0, 0, 0, 200), 1.5f);
    DrawLabel(dl, ImVec2(p.x - dx * 20, p.y - dy * 20 + 8), label, color);
}

void DrawToast(ImDrawList* dl, float screenW) {
    if (GetTickCount64() > s_toastUntil) return;
    const ImVec2 size = ImGui::CalcTextSize(s_toast.c_str());
    const ImVec2 min(screenW / 2 - size.x / 2 - 10, 40);
    const ImVec2 max(screenW / 2 + size.x / 2 + 10, 40 + size.y + 12);
    dl->AddRectFilled(min, max, IM_COL32(0, 0, 0, 170), 6.0f);
    dl->AddText(ImVec2(min.x + 10, min.y + 6), IM_COL32_WHITE, s_toast.c_str());
}

// ---------------------------------------------------------------- menu

const char* KeyName(int vk) {
    static char name[32];
    const UINT scan = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    if (!scan || !GetKeyNameTextA(static_cast<LONG>(scan << 16), name, sizeof(name))) {
        snprintf(name, sizeof(name), "0x%X", vk);
    }
    return name;
}

void DrawStatus(const GameState& state) {
    {
        std::lock_guard lock(s_statusMutex);
        if (!s_status.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", s_status.c_str());
    }
    ImGui::TextDisabled("Minecraft %s, profile: %s", game::GameVersion().c_str(),
                        g_offsets.name.empty() ? "none" : g_offsets.name.c_str());
    if (!state.valid) {
        ImGui::TextDisabled("Not in a world (or the offsets don't match this Minecraft version).");
        return;
    }
    const Vec3 feet = FeetBlock(state);
    ImGui::Text("Position: %.0f %.0f %.0f   %s", feet.x, feet.y, feet.z,
                state.dimension[0] ? DimensionLabel(state.dimension) : "");
    if (state.world[0]) ImGui::Text("World: %s", state.world);
}

void DrawEditor(const GameState& state) {
    if (s_editIndex < 0 || s_editIndex >= static_cast<int>(g_waypoints.size())) return;

    ImGui::SeparatorText("Edit waypoint");
    ImGui::InputText("Name##edit", s_editName, sizeof(s_editName));
    ImGui::InputInt3("X Y Z##edit", s_editCoords);
    ImGui::ColorEdit3("Color##edit", s_edit.color, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    ImGui::Checkbox("Always load (every world)##edit", &s_edit.always);

    // "Current" makes no sense for an existing waypoint, so start the combo at "Any".
    if (ImGui::BeginCombo("Dimension##edit", kDimensionChoices[s_editDimension].label)) {
        for (int i = 1; i < IM_ARRAYSIZE(kDimensionChoices); ++i)
            if (ImGui::Selectable(kDimensionChoices[i].label, s_editDimension == i)) s_editDimension = i;
        ImGui::EndCombo();
    }
    ImGui::Text("World: %s", s_edit.world.empty() ? "(any)" : s_edit.world.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.world[0]);
    if (ImGui::SmallButton("Set to this world")) s_edit.world = state.world;
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!state.valid);
    if (ImGui::Button("Use my position##edit")) {
        const Vec3 feet = FeetBlock(state);
        s_editCoords[0] = static_cast<int>(feet.x);
        s_editCoords[1] = static_cast<int>(feet.y);
        s_editCoords[2] = static_cast<int>(feet.z);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        s_edit.name = s_editName[0] ? s_editName : "Waypoint";
        s_edit.pos = {static_cast<float>(s_editCoords[0]), static_cast<float>(s_editCoords[1]),
                      static_cast<float>(s_editCoords[2])};
        s_edit.dimension = kDimensionChoices[s_editDimension].id;
        g_waypoints[s_editIndex] = s_edit;
        SaveWaypoints();
        s_editIndex = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) s_editIndex = -1;
}

void StartEdit(int index) {
    s_editIndex = index;
    s_edit = g_waypoints[index];
    snprintf(s_editName, sizeof(s_editName), "%s", s_edit.name.c_str());
    s_editCoords[0] = static_cast<int>(s_edit.pos.x);
    s_editCoords[1] = static_cast<int>(s_edit.pos.y);
    s_editCoords[2] = static_cast<int>(s_edit.pos.z);
    s_editDimension = DimensionIndex(s_edit.dimension);
}

void DrawListTab(const GameState& state) {
    int removeIndex = -1;
    bool changed = false;
    const float tableHeight = s_editIndex >= 0 ? 170.0f : 0.0f;
    if (ImGui::BeginTable("list", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                          ImVec2(0, tableHeight))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Show", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("X Y Z");
        ImGui::TableSetupColumn("Where");
        ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(g_waypoints.size()); ++i) {
            Waypoint& w = g_waypoints[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            changed |= ImGui::Checkbox("##show", &w.visible);
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(w.color[0], w.color[1], w.color[2], 1), "%s", w.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.0f %.0f %.0f", w.pos.x, w.pos.y, w.pos.z);
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", w.always ? "Always, " : "", DimensionLabel(w.dimension));
            if (ImGui::IsItemHovered() && !w.world.empty()) ImGui::SetTooltip("World: %s", w.world.c_str());
            ImGui::TableNextColumn();
            Vec3 pos;
            bool converted = false;
            if (state.valid && DisplayPosition(w, state, pos, converted)) {
                ImGui::Text("%.0fm", Distance({pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, state.origin));
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Edit")) StartEdit(i);
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) removeIndex = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeIndex >= 0) {
        g_waypoints.erase(g_waypoints.begin() + removeIndex);
        if (s_editIndex == removeIndex) s_editIndex = -1;
        else if (s_editIndex > removeIndex) --s_editIndex;
        changed = true;
    }
    if (changed) SaveWaypoints();
    DrawEditor(state);
}

void DrawAddTab(const GameState& state) {
    ImGui::InputText("Name", s_newName, sizeof(s_newName));
    ImGui::InputInt3("X Y Z", s_newCoords);
    ImGui::ColorEdit3("Color", s_newColor, ImGuiColorEditFlags_NoInputs);
    if (ImGui::BeginCombo("Dimension", kDimensionChoices[s_newDimension].label)) {
        for (int i = 0; i < IM_ARRAYSIZE(kDimensionChoices); ++i)
            if (ImGui::Selectable(kDimensionChoices[i].label, s_newDimension == i)) s_newDimension = i;
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Always load (every world)", &s_newAlways);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shown in every world and on every server, every time you inject.\n"
                          "Otherwise it only shows in the world you made it in.");
    }

    ImGui::BeginDisabled(!state.valid);
    if (ImGui::Button("Use my position")) {
        const Vec3 feet = FeetBlock(state);
        s_newCoords[0] = static_cast<int>(feet.x);
        s_newCoords[1] = static_cast<int>(feet.y);
        s_newCoords[2] = static_cast<int>(feet.z);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Overworld -> Nether (/8)")) {
        s_newCoords[0] = static_cast<int>(std::floor(s_newCoords[0] / 8.0f));
        s_newCoords[2] = static_cast<int>(std::floor(s_newCoords[2] / 8.0f));
    }
    ImGui::SameLine();
    if (ImGui::Button("Nether -> Overworld (x8)")) {
        s_newCoords[0] *= 8;
        s_newCoords[2] *= 8;
    }

    if (ImGui::Button("Add", ImVec2(120, 0))) {
        Waypoint w;
        w.name = s_newName[0] ? s_newName : "Waypoint";
        w.pos = {static_cast<float>(s_newCoords[0]), static_cast<float>(s_newCoords[1]),
                 static_cast<float>(s_newCoords[2])};
        std::copy(std::begin(s_newColor), std::end(s_newColor), w.color);
        const char* dim = kDimensionChoices[s_newDimension].id;
        w.dimension = dim ? dim : state.dimension;
        w.world = s_newAlways ? "" : state.world;
        w.always = s_newAlways;
        g_waypoints.push_back(w);
        SaveWaypoints();
        Toast("Added " + w.name);
    }
}

void DrawSettingsTab() {
    bool changed = false;
    changed |= ImGui::Checkbox("Beam", &g_settings.showBeam);
    changed |= ImGui::Checkbox("Show distance", &g_settings.showDistance);
    changed |= ImGui::Checkbox("Nether conversion", &g_settings.netherConversion);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show overworld waypoints in the Nether at /8, and Nether waypoints in the overworld at x8.");
    }
    changed |= ImGui::Checkbox("Death waypoints", &g_settings.deathWaypoints);
    ImGui::SetNextItemWidth(150);
    changed |= ImGui::SliderInt("Keep last deaths", &g_settings.maxDeathWaypoints, 1, 10);
    if (changed) SaveSettings();

    if (ImGui::Button("Delete all death waypoints")) {
        std::erase_if(g_waypoints, [](const Waypoint& w) { return w.death; });
        s_editIndex = -1;
        SaveWaypoints();
    }

    ImGui::SeparatorText("Keys");
    ImGui::Text("Open menu: %s", KeyName(g_config.menuKey));
    ImGui::Text("Add waypoint at your position: %s", KeyName(g_config.addWaypointKey));
    ImGui::TextDisabled("Change keys in %%LOCALAPPDATA%%\\BedrockWaypoints\\config.ini");
}

void DrawMenu(const GameState& state) {
    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(620, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Waypoints", &s_menuOpen)) {
        ImGui::End();
        return;
    }

    DrawStatus(state);
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Waypoints")) {
            DrawListTab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Add")) {
            DrawAddTab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            DrawSettingsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace

namespace ui {

void Setup() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    ImGui::GetIO().FontGlobalScale = 1.2f;
}

void Draw() {
    const GameState state = game::State();
    PollKeys(state);
    HandleDeath();
    ImGui::GetIO().MouseDrawCursor = s_menuOpen;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (state.valid) {
        for (const auto& w : g_waypoints)
            if (w.visible) DrawWaypoint(dl, state, w, display.x, display.y);
    }
    DrawToast(dl, display.x);

    if (s_menuOpen) DrawMenu(state);
}

bool MenuOpen() {
    return s_menuOpen;
}

void SetStatus(const std::string& status) {
    std::lock_guard lock(s_statusMutex);
    s_status = status;
}

} // namespace ui
