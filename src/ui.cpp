#include "ui.h"

#include "config.h"
#include "game.h"
#include "waypoints.h"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace {

constexpr float kEyeHeight = 1.62f;

bool s_menuOpen = false;
bool s_keyWasDown = false;
bool s_showBeam = true;
bool s_showDistance = true;

std::mutex s_statusMutex;
std::string s_status;

char s_newName[64] = "Home";
int s_newCoords[3] = {0, 64, 0};
float s_newColor[3] = {0.2f, 0.8f, 1.0f};

float Distance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void PollMenuKey() {
    const bool down = (GetAsyncKeyState(g_config.menuKey) & 0x8000) != 0;
    const HWND gameWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
    if (down && !s_keyWasDown && GetForegroundWindow() == gameWindow) s_menuOpen = !s_menuOpen;
    s_keyWasDown = down;
}

void DrawLabel(ImDrawList* dl, ImVec2 anchor, const char* text, ImU32 color) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 min(anchor.x - size.x / 2 - 5, anchor.y - size.y - 6);
    const ImVec2 max(anchor.x + size.x / 2 + 5, anchor.y);
    dl->AddRectFilled(min, max, IM_COL32(0, 0, 0, 150), 4.0f);
    dl->AddRectFilled(ImVec2(min.x, max.y - 2), max, color, 0.0f);
    dl->AddText(ImVec2(min.x + 5, min.y + 2), IM_COL32_WHITE, text);
}

void DrawWaypoint(ImDrawList* dl, const CameraState& cam, const Waypoint& w, float screenW, float screenH) {
    // Waypoints are block coordinates; aim at the middle of the block.
    const Vec3 center{w.pos.x + 0.5f, w.pos.y + 0.5f, w.pos.z + 0.5f};
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(w.color[0], w.color[1], w.color[2], 1.0f));

    char label[128];
    if (s_showDistance) {
        snprintf(label, sizeof(label), "%s  %.0fm", w.name.c_str(), Distance(center, cam.origin));
    } else {
        snprintf(label, sizeof(label), "%s", w.name.c_str());
    }

    float sx, sy;
    const bool onScreen = game::WorldToScreen(cam, center, screenW, screenH, sx, sy) && sx >= 0 && sx <= screenW &&
                          sy >= 0 && sy <= screenH;

    if (onScreen) {
        if (s_showBeam) {
            float bx, by, tx, ty;
            const Vec3 bottom{center.x, w.pos.y, center.z};
            const Vec3 top{center.x, w.pos.y + 100.0f, center.z};
            if (game::WorldToScreen(cam, bottom, screenW, screenH, bx, by) &&
                game::WorldToScreen(cam, top, screenW, screenH, tx, ty)) {
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
    const Vec3 v = game::ToViewSpace(cam, center);
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

void DrawMenu(const CameraState& cam) {
    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Waypoints", &s_menuOpen)) {
        ImGui::End();
        return;
    }

    {
        std::lock_guard lock(s_statusMutex);
        if (!s_status.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", s_status.c_str());
    }
    if (cam.valid) {
        ImGui::Text("Your position: %.0f %.0f %.0f", std::floor(cam.origin.x),
                    std::floor(cam.origin.y - kEyeHeight), std::floor(cam.origin.z));
    } else {
        ImGui::TextDisabled("Not in a world (or the offsets don't match this Minecraft version).");
    }

    ImGui::SeparatorText("Add waypoint");
    ImGui::InputText("Name", s_newName, sizeof(s_newName));
    ImGui::InputInt3("X Y Z", s_newCoords);
    ImGui::ColorEdit3("Color", s_newColor, ImGuiColorEditFlags_NoInputs);
    ImGui::BeginDisabled(!cam.valid);
    if (ImGui::Button("Use my position")) {
        s_newCoords[0] = static_cast<int>(std::floor(cam.origin.x));
        s_newCoords[1] = static_cast<int>(std::floor(cam.origin.y - kEyeHeight));
        s_newCoords[2] = static_cast<int>(std::floor(cam.origin.z));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Add")) {
        Waypoint w;
        w.name = s_newName[0] ? s_newName : "Waypoint";
        w.pos = {static_cast<float>(s_newCoords[0]), static_cast<float>(s_newCoords[1]),
                 static_cast<float>(s_newCoords[2])};
        std::copy(std::begin(s_newColor), std::end(s_newColor), w.color);
        g_waypoints.push_back(w);
        SaveWaypoints();
    }

    ImGui::SeparatorText("Display");
    ImGui::Checkbox("Beam", &s_showBeam);
    ImGui::SameLine();
    ImGui::Checkbox("Distance", &s_showDistance);

    ImGui::SeparatorText("Waypoints");
    int removeIndex = -1;
    bool changed = false;
    if (ImGui::BeginTable("list", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Show", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("X Y Z");
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
            if (cam.valid) ImGui::Text("%.0fm", Distance({w.pos.x + 0.5f, w.pos.y + 0.5f, w.pos.z + 0.5f}, cam.origin));
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Delete")) removeIndex = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeIndex >= 0) {
        g_waypoints.erase(g_waypoints.begin() + removeIndex);
        changed = true;
    }
    if (changed) SaveWaypoints();

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
    PollMenuKey();
    ImGui::GetIO().MouseDrawCursor = s_menuOpen;

    const CameraState cam = game::Camera();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (cam.valid) {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        for (const auto& w : g_waypoints)
            if (w.visible) DrawWaypoint(dl, cam, w, display.x, display.y);
    }

    if (s_menuOpen) DrawMenu(cam);
}

bool MenuOpen() {
    return s_menuOpen;
}

void SetStatus(const std::string& status) {
    std::lock_guard lock(s_statusMutex);
    s_status = status;
}

} // namespace ui
