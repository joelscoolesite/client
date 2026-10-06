#include "ui.h"

#include "config.h"
#include "game.h"
#include "modules.h"
#include "waypoints.h"

#include <windows.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>

namespace {

enum class Page { Hud, Visual, Utility, Waypoints, Settings };

bool s_menuOpen = false;
bool s_hudEdit = false;
bool s_menuKeyWasDown = false;
bool s_addKeyWasDown = false;
float s_menuAlpha = 0.0f;
Page s_page = Page::Hud;
char s_search[64] = "";

ImFont* s_fontBold = nullptr;
ImFont* s_fontTitle = nullptr;

std::mutex s_statusMutex;
std::string s_status;

struct Notification {
    std::string title;
    std::string text;
    double start;
};
std::deque<Notification> s_notifications;

// Key binding: the setting being bound and which keys were already down when binding started.
int* s_bindTarget = nullptr;
bool s_bindPrev[256];

// Waypoints "Add" form
char s_newName[64] = "Home";
int s_newCoords[3] = {0, 64, 0};
float s_newColor[3] = {0.2f, 0.8f, 1.0f};
int s_newDimension = 0; // index into kDimensionChoices
bool s_newAlways = false;

// Waypoint editor
int s_editIndex = -1;
Waypoint s_edit;
char s_editName[64] = "";
int s_editCoords[3] = {0, 0, 0};
int s_editDimension = 0;

char s_profileName[64] = "";

struct DimensionChoice {
    const char* label;
    const char* id; // stored in Waypoint::dimension, nullptr = current
};
constexpr DimensionChoice kDimensionChoices[] = {
    {"Current", nullptr}, {"Any", ""}, {"Overworld", "Overworld"}, {"Nether", "Nether"}, {"The End", "TheEnd"},
};

constexpr float kPalette[][3] = {
    {0.2f, 0.8f, 1.0f}, {1.0f, 0.8f, 0.2f}, {0.4f, 1.0f, 0.4f},
    {1.0f, 0.4f, 0.9f}, {1.0f, 0.5f, 0.2f}, {0.6f, 0.6f, 1.0f},
};

constexpr float kAccentPresets[][3] = {
    {0.56f, 0.42f, 1.0f}, {0.25f, 0.6f, 1.0f}, {0.2f, 0.85f, 0.6f},
    {1.0f, 0.45f, 0.45f}, {1.0f, 0.65f, 0.2f}, {0.95f, 0.4f, 0.8f},
};

ImVec4 Accent(float alpha = 1.0f) {
    return ImVec4(g_theme.accent[0], g_theme.accent[1], g_theme.accent[2], alpha);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

float Distance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

HWND GameWindow() {
    return static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
}

Vec3 FeetBlock(const GameState& state) {
    const Vec3 eye = state.hasPlayer ? state.playerPos : state.origin;
    return {std::floor(eye.x), std::floor(eye.y - kEyeHeight), std::floor(eye.z)};
}

const char* KeyName(int vk) {
    static char names[4][32];
    static int slot = 0;
    char* name = names[slot++ % 4];
    if (vk == 0) return "None";
    if (vk == VK_LBUTTON) return "Left Mouse";
    if (vk == VK_RBUTTON) return "Right Mouse";
    if (vk == VK_MBUTTON) return "Middle Mouse";
    UINT scan = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    LONG lParam = static_cast<LONG>(scan << 16);
    if (vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
        vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_RCONTROL || vk == VK_RMENU)
        lParam |= 1 << 24; // extended key
    if (!scan || !GetKeyNameTextA(lParam, name, 32)) snprintf(name, 32, "Key 0x%X", vk);
    return name;
}

// ---------------------------------------------------------------- style

void ApplyStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 14;
    st.ChildRounding = 10;
    st.FrameRounding = 7;
    st.GrabRounding = 7;
    st.PopupRounding = 8;
    st.ScrollbarRounding = 8;
    st.TabRounding = 7;
    st.WindowPadding = ImVec2(16, 14);
    st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(10, 8);
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 0;
    st.PopupBorderSize = 0;
    st.GrabMinSize = 10;
    st.ScrollbarSize = 10;
    st.SeparatorTextBorderSize = 1;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = ImVec4(0.93f, 0.93f, 0.96f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.56f, 0.63f, 1);
    c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.075f, 0.10f, 0.97f);
    c[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.115f, 0.15f, 1);
    c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.105f, 0.14f, 0.98f);
    c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.165f, 0.21f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.205f, 0.26f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.23f, 0.235f, 0.30f, 1);
    c[ImGuiCol_Button] = ImVec4(0.17f, 0.175f, 0.23f, 1);
    c[ImGuiCol_ButtonHovered] = Accent(0.75f);
    c[ImGuiCol_ButtonActive] = Accent(1.0f);
    c[ImGuiCol_Header] = Accent(0.30f);
    c[ImGuiCol_HeaderHovered] = Accent(0.45f);
    c[ImGuiCol_HeaderActive] = Accent(0.6f);
    c[ImGuiCol_SliderGrab] = Accent(0.9f);
    c[ImGuiCol_SliderGrabActive] = Accent(1.0f);
    c[ImGuiCol_CheckMark] = Accent(1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.14f, 0.145f, 0.19f, 1);
    c[ImGuiCol_TabHovered] = Accent(0.6f);
    c[ImGuiCol_TabSelected] = Accent(0.45f);
    c[ImGuiCol_TabSelectedOverline] = Accent(1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.22f, 0.22f, 0.28f, 1);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.13f, 0.135f, 0.18f, 1);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.2f, 0.2f, 0.26f, 1);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.25f, 0.32f, 1);
    c[ImGuiCol_ScrollbarGrabHovered] = Accent(0.6f);
    c[ImGuiCol_ScrollbarGrabActive] = Accent(0.9f);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TextSelectedBg] = Accent(0.35f);
}

void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    wchar_t windows[MAX_PATH];
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::filesystem::path fonts = std::filesystem::path(windows) / L"Fonts";
    const std::string regular = (fonts / L"segoeui.ttf").string();
    const std::string bold = (fonts / L"segoeuib.ttf").string();

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    if (std::filesystem::exists(regular) && std::filesystem::exists(bold)) {
        io.Fonts->AddFontFromFileTTF(regular.c_str(), 18.0f, &cfg);
        s_fontBold = io.Fonts->AddFontFromFileTTF(bold.c_str(), 18.0f, &cfg);
        s_fontTitle = io.Fonts->AddFontFromFileTTF(bold.c_str(), 26.0f, &cfg);
    } else {
        cfg.SizePixels = 16;
        io.Fonts->AddFontDefault(&cfg);
    }
    if (!s_fontBold) s_fontBold = io.Fonts->Fonts[0];
    if (!s_fontTitle) s_fontTitle = s_fontBold;
}

// ---------------------------------------------------------------- widgets

// iOS-style switch. Returns true when clicked.
bool ToggleSwitch(const char* id, bool* value, bool enabled = true) {
    const float h = ImGui::GetFrameHeight() * 0.78f, w = h * 1.85f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    ImGui::EndDisabled();
    if (clicked) *value = !*value;

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetItemID();
    float t = storage->GetFloat(key, *value ? 1.0f : 0.0f);
    t = Lerp(t, *value ? 1.0f : 0.0f, ImGui::GetIO().DeltaTime * 14);
    storage->SetFloat(key, t);

    const ImVec4 off(0.24f, 0.245f, 0.30f, 1), on = Accent();
    ImVec4 bg(Lerp(off.x, on.x, t), Lerp(off.y, on.y, t), Lerp(off.z, on.z, t), enabled ? 1.0f : 0.4f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::ColorConvertFloat4ToU32(bg), h / 2);
    const float r = h / 2 - 2.5f;
    dl->AddCircleFilled(ImVec2(p.x + h / 2 + t * (w - h), p.y + h / 2), r, IM_COL32(255, 255, 255, enabled ? 255 : 120));
    return clicked;
}

void KeyButton(const char* id, int* key) {
    const bool binding = s_bindTarget == key;
    char label[64];
    snprintf(label, sizeof(label), "%s##%s", binding ? "Press a key..." : KeyName(*key), id);
    if (binding) ImGui::PushStyleColor(ImGuiCol_Button, Accent(0.6f));
    if (ImGui::Button(label, ImVec2(140, 0))) {
        s_bindTarget = key;
        for (int vk = 0; vk < 256; ++vk) s_bindPrev[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
    if (binding) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click, then press a key. Escape clears it.");
}

void PollKeyBinding() {
    if (!s_bindTarget) return;
    for (int vk = 3; vk < 256; ++vk) {
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (down && !s_bindPrev[vk]) {
            if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU ||
                vk == VK_RMENU)
                continue; // the generic VK_SHIFT/VK_CONTROL/VK_MENU codes are what the game sees
            *s_bindTarget = vk == VK_ESCAPE ? 0 : vk;
            s_bindTarget = nullptr;
            return;
        }
        s_bindPrev[vk] = down;
    }
}

// Label on the left, control on the right of the available width.
void RowLabel(const char* label, float controlWidth) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(std::max(ImGui::GetContentRegionMax().x - controlWidth, ImGui::GetCursorPosX()));
}

void SectionTitle(const char* text) {
    ImGui::PushFont(s_fontBold);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

// ---------------------------------------------------------------- notifications

void DrawNotifications(ImVec2 screen) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const double now = ImGui::GetTime();
    constexpr double kLife = 3.5;
    while (!s_notifications.empty() && now - s_notifications.front().start > kLife) s_notifications.pop_front();

    float y = screen.y - 24;
    for (auto it = s_notifications.rbegin(); it != s_notifications.rend(); ++it) {
        const float age = static_cast<float>(now - it->start);
        const float in = std::min(age / 0.25f, 1.0f);
        const float out = std::clamp((static_cast<float>(kLife) - age) / 0.4f, 0.0f, 1.0f);
        const float ease = 1 - (1 - in) * (1 - in) * (1 - in);
        const float alpha = out;

        const float width = 300, pad = 12;
        const ImVec2 titleSize = s_fontBold->CalcTextSizeA(18, FLT_MAX, 0, it->title.c_str());
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(16, FLT_MAX, width - pad * 2 - 6, it->text.c_str());
        const float height = pad * 2 + titleSize.y + 2 + textSize.y + 4;
        const float x = screen.x - 24 - width * ease + (1 - ease) * 40;
        y -= height;

        const ImVec2 min(x, y), max(x + width, y + height);
        dl->AddRectFilled(min, max, IM_COL32(16, 17, 24, static_cast<int>(240 * alpha)), 10);
        dl->AddRectFilled(ImVec2(min.x, min.y + 10), ImVec2(min.x + 3, max.y - 10), AccentColor(alpha), 2);
        dl->AddText(s_fontBold, 18, ImVec2(min.x + pad + 6, min.y + pad), IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)),
                    it->title.c_str());
        dl->AddText(ImGui::GetFont(), 16, ImVec2(min.x + pad + 6, min.y + pad + titleSize.y + 2),
                    IM_COL32(190, 192, 205, static_cast<int>(255 * alpha)), it->text.c_str(), nullptr,
                    width - pad * 2 - 6);
        const float progress = 1 - age / static_cast<float>(kLife);
        dl->AddRectFilled(ImVec2(min.x + 10, max.y - 3), ImVec2(min.x + 10 + (width - 20) * progress, max.y - 1),
                          AccentColor(0.7f * alpha), 1);
        y -= 8;
    }
}

// ---------------------------------------------------------------- waypoints in the world

void DrawLabel(ImDrawList* dl, ImVec2 anchor, const char* text, ImU32 color) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 min(anchor.x - size.x / 2 - 7, anchor.y - size.y - 8);
    const ImVec2 max(anchor.x + size.x / 2 + 7, anchor.y);
    dl->AddRectFilled(min, max, IM_COL32(12, 12, 18, 170), 6.0f);
    dl->AddRectFilled(ImVec2(min.x + 4, max.y - 2), ImVec2(max.x - 4, max.y), color, 1.0f);
    dl->AddText(ImVec2(min.x + 7, min.y + 3), IM_COL32_WHITE, text);
}

void DrawWaypoint(ImDrawList* dl, const GameState& state, const Waypoint& w, float screenW, float screenH) {
    Vec3 pos;
    bool converted = false;
    if (!WaypointDisplayPosition(w, state, pos, converted)) return;

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
            if (game::WorldToScreen(state, {center.x, pos.y, center.z}, screenW, screenH, bx, by) &&
                game::WorldToScreen(state, {center.x, pos.y + 100.0f, center.z}, screenW, screenH, tx, ty)) {
                dl->AddLine(ImVec2(bx, by), ImVec2(tx, ty),
                            ImGui::ColorConvertFloat4ToU32(ImVec4(w.color[0], w.color[1], w.color[2], 0.45f)), 3.0f);
            }
        }
        dl->AddCircleFilled(ImVec2(sx, sy), 6.0f, color);
        dl->AddCircle(ImVec2(sx, sy), 6.0f, IM_COL32(0, 0, 0, 200), 0, 2.0f);
        DrawLabel(dl, ImVec2(sx, sy - 10), label, color);
        return;
    }

    // Off screen or behind us: arrow on the screen edge.
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

void AddWaypointAtPlayer(const GameState& state) {
    if (!state.valid) return;
    Waypoint w;
    char name[32];
    snprintf(name, sizeof(name), "Waypoint %d", static_cast<int>(g_waypoints.size()) + 1);
    w.name = name;
    w.pos = FeetBlock(state);
    w.world = state.world;
    w.dimension = state.dimension;
    const auto& c = kPalette[g_waypoints.size() % IM_ARRAYSIZE(kPalette)];
    std::copy(std::begin(c), std::end(c), w.color);
    g_waypoints.push_back(w);
    SaveWaypoints();
    char coords[64];
    snprintf(coords, sizeof(coords), "%.0f %.0f %.0f", w.pos.x, w.pos.y, w.pos.z);
    ui::Notify("Added " + w.name, coords);
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

    int deaths = 0;
    for (const auto& x : g_waypoints)
        if (x.death) ++deaths;
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
    char coords[64];
    snprintf(coords, sizeof(coords), "%.0f %.0f %.0f", w.pos.x, w.pos.y, w.pos.z);
    ui::Notify("Death waypoint saved", coords);
}

// ---------------------------------------------------------------- Waypoints page

int DimensionIndex(const std::string& id) {
    for (int i = 1; i < IM_ARRAYSIZE(kDimensionChoices); ++i)
        if (id == kDimensionChoices[i].id) return i;
    return 1;
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

void DrawEditor(const GameState& state) {
    if (s_editIndex < 0 || s_editIndex >= static_cast<int>(g_waypoints.size())) return;
    ImGui::SeparatorText("Edit waypoint");
    ImGui::InputText("Name##edit", s_editName, sizeof(s_editName));
    ImGui::InputInt3("X Y Z##edit", s_editCoords);
    ImGui::ColorEdit3("Color##edit", s_edit.color, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    ImGui::Checkbox("Always load (every world)##edit", &s_edit.always);
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

void DrawChatFound(const GameState& state) {
    if (g_chatFound.empty()) return;
    ImGui::SeparatorText("Coordinates from chat");
    for (int i = static_cast<int>(g_chatFound.size()) - 1; i >= 0; --i) {
        const ChatCoords& c = g_chatFound[i];
        ImGui::PushID(1000 + i);
        if (ImGui::SmallButton("Add")) {
            Waypoint w;
            w.name = "Chat";
            w.pos = c.pos;
            w.world = state.world;
            w.dimension = state.dimension;
            w.color[0] = 1.0f;
            w.color[1] = 0.85f;
            w.color[2] = 0.3f;
            g_waypoints.push_back(w);
            SaveWaypoints();
            g_chatFound.erase(g_chatFound.begin() + i);
            ImGui::PopID();
            break;
        }
        ImGui::SameLine();
        ImGui::Text("%.0f %.0f %.0f", c.pos.x, c.pos.y, c.pos.z);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", c.text.c_str());
        ImGui::PopID();
    }
}

void DrawWaypointList(const GameState& state) {
    DrawChatFound(state);
    int removeIndex = -1;
    bool changed = false;
    const float tableHeight = s_editIndex >= 0 ? 170.0f : 0.0f;
    if (ImGui::BeginTable("list", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX,
                          ImVec2(0, tableHeight))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("X Y Z");
        ImGui::TableSetupColumn("Where");
        ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(g_waypoints.size()); ++i) {
            Waypoint& w = g_waypoints[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            changed |= ToggleSwitch("##show", &w.visible);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(w.color[0], w.color[1], w.color[2], 1), "%s", w.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.0f %.0f %.0f", w.pos.x, w.pos.y, w.pos.z);
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", w.always ? "Always, " : "", DimensionLabel(w.dimension));
            if (ImGui::IsItemHovered() && !w.world.empty()) ImGui::SetTooltip("World: %s", w.world.c_str());
            ImGui::TableNextColumn();
            Vec3 pos;
            bool converted = false;
            if (state.valid && WaypointDisplayPosition(w, state, pos, converted)) {
                ImGui::Text("%.0fm", Distance({pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, state.origin));
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Edit")) StartEdit(i);
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) {
                char text[200];
                snprintf(text, sizeof(text), "%s: %.0f %.0f %.0f%s%s", w.name.c_str(), w.pos.x, w.pos.y, w.pos.z,
                         w.dimension.empty() ? "" : " in the ", w.dimension.empty() ? "" : DimensionLabel(w.dimension));
                ImGui::SetClipboardText(text);
                ui::Notify("Copied", "Paste it in chat with Ctrl+V");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) removeIndex = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (g_waypoints.empty()) ImGui::TextDisabled("No waypoints yet. Press F7 in game or use the Add tab.");
    if (removeIndex >= 0) {
        g_waypoints.erase(g_waypoints.begin() + removeIndex);
        if (s_editIndex == removeIndex) s_editIndex = -1;
        else if (s_editIndex > removeIndex) --s_editIndex;
        changed = true;
    }
    if (changed) SaveWaypoints();
    DrawEditor(state);
}

void DrawWaypointAdd(const GameState& state) {
    ImGui::InputText("Name", s_newName, sizeof(s_newName));
    ImGui::InputInt3("X Y Z", s_newCoords);
    ImGui::ColorEdit3("Color", s_newColor, ImGuiColorEditFlags_NoInputs);
    if (ImGui::BeginCombo("Dimension", kDimensionChoices[s_newDimension].label)) {
        for (int i = 0; i < IM_ARRAYSIZE(kDimensionChoices); ++i)
            if (ImGui::Selectable(kDimensionChoices[i].label, s_newDimension == i)) s_newDimension = i;
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Always load (every world)", &s_newAlways);
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

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, Accent(0.85f));
    if (ImGui::Button("Add waypoint", ImVec2(160, 0))) {
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
        ui::Notify("Added " + w.name, "Waypoint saved");
    }
    ImGui::PopStyleColor();
}

void DrawWaypointOptions() {
    bool changed = false;
    RowLabel("Beam", 60);
    changed |= ToggleSwitch("##beam", &g_settings.showBeam);
    RowLabel("Show distance", 60);
    changed |= ToggleSwitch("##dist", &g_settings.showDistance);
    RowLabel("Nether conversion (/8 and x8)", 60);
    changed |= ToggleSwitch("##nether", &g_settings.netherConversion);
    RowLabel("Death waypoints", 60);
    changed |= ToggleSwitch("##death", &g_settings.deathWaypoints);
    RowLabel("Keep last deaths", 200);
    ImGui::SetNextItemWidth(200);
    changed |= ImGui::SliderInt("##maxdeaths", &g_settings.maxDeathWaypoints, 1, 10);
    if (changed) SaveSettings();

    ImGui::Spacing();
    if (ImGui::Button("Delete all death waypoints")) {
        std::erase_if(g_waypoints, [](const Waypoint& w) { return w.death; });
        s_editIndex = -1;
        SaveWaypoints();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Quick waypoint key: %s", KeyName(g_config.addWaypointKey));
}

void DrawWaypointsPage(const GameState& state) {
    if (ImGui::BeginTabBar("wp")) {
        if (ImGui::BeginTabItem("List")) {
            DrawWaypointList(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Add")) {
            DrawWaypointAdd(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Options")) {
            DrawWaypointOptions();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// ---------------------------------------------------------------- module pages

void DrawSetting(Module* m, Setting& st) {
    ImGui::PushID(st.id.c_str());
    switch (st.type) {
    case Setting::Type::Bool:
        RowLabel(st.label.c_str(), 54);
        ToggleSwitch("##v", static_cast<bool*>(st.value));
        break;
    case Setting::Type::Float:
        RowLabel(st.label.c_str(), 180);
        ImGui::SetNextItemWidth(180);
        ImGui::SliderFloat("##v", static_cast<float*>(st.value), st.min, st.max, st.format);
        break;
    case Setting::Type::Int:
        RowLabel(st.label.c_str(), 180);
        ImGui::SetNextItemWidth(180);
        ImGui::SliderInt("##v", static_cast<int*>(st.value), static_cast<int>(st.min), static_cast<int>(st.max));
        break;
    case Setting::Type::Key:
        RowLabel(st.label.c_str(), 140);
        KeyButton((m->name + st.id).c_str(), static_cast<int*>(st.value));
        break;
    case Setting::Type::Color:
        RowLabel(st.label.c_str(), 30);
        ImGui::ColorEdit4("##v", static_cast<float*>(st.value),
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        break;
    }
    ImGui::PopID();
}

void ModuleCard(Module* m) {
    ImGui::PushID(m->name.c_str());
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID openId = ImGui::GetID("open");
    bool open = storage->GetBool(openId, false);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, m->enabled ? ImVec4(0.13f, 0.135f, 0.18f, 1) : ImVec4(0.11f, 0.115f, 0.15f, 1));
    ImGui::BeginChild("card", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);

    const bool available = m->Available();
    ImGui::PushFont(s_fontBold);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(m->name.c_str());
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight() * 0.78f * 1.85f);
    bool enabled = m->enabled;
    if (ToggleSwitch("##on", &enabled, available)) {
        m->SetEnabled(enabled);
        ui::Notify(m->name, enabled ? "Enabled" : "Disabled");
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", m->description.c_str());
    ImGui::PopStyleColor();
    if (!available) ImGui::TextColored(ImVec4(1, 0.5f, 0.45f, 1), "Not available for this Minecraft version");

    // Expand arrow for settings.
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::SmallButton(open ? "Hide settings" : "Settings")) {
        open = !open;
        storage->SetBool(openId, open);
    }
    ImGui::PopStyleColor();
    if (m->key) {
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", KeyName(m->key));
    }

    if (open) {
        ImGui::Separator();
        RowLabel("Toggle key", 140);
        KeyButton((m->name + "bind").c_str(), &m->key);
        if (m->hud) {
            RowLabel("Size", 180);
            ImGui::SetNextItemWidth(180);
            ImGui::SliderFloat("##scale", &m->hudScale, 0.5f, 2.5f, "%.2fx");
        }
        for (Setting& st : m->settings) DrawSetting(m, st);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Accent edge on enabled cards.
    if (m->enabled) {
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x, min.y + 12), ImVec2(min.x + 3, max.y - 12),
                                                  AccentColor(), 2);
    }
    ImGui::PopID();
}

bool MatchesSearch(const Module* m) {
    if (!s_search[0]) return true;
    std::string hay = m->name + " " + m->description, needle = s_search;
    std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return std::tolower(c); });
    return hay.find(needle) != std::string::npos;
}

void DrawModulePage(Category category) {
    if (ImGui::BeginTable("grid", 2, ImGuiTableFlags_SizingStretchSame)) {
        for (Module* m : modules::All()) {
            if ((s_search[0] ? false : m->category != category) || !MatchesSearch(m)) continue;
            ImGui::TableNextColumn();
            ModuleCard(m);
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------- settings page

void DrawSettingsPage(const GameState& state) {
    SectionTitle("Appearance");
    RowLabel("Accent colour", 30 + 6 * 30.0f);
    for (int i = 0; i < IM_ARRAYSIZE(kAccentPresets); ++i) {
        ImGui::PushID(i);
        const auto& p = kAccentPresets[i];
        if (ImGui::ColorButton("##preset", ImVec4(p[0], p[1], p[2], 1), ImGuiColorEditFlags_NoTooltip, ImVec2(22, 22))) {
            std::copy(std::begin(p), std::end(p), g_theme.accent);
            ApplyStyle();
        }
        ImGui::SameLine();
        ImGui::PopID();
    }
    if (ImGui::ColorEdit3("##accent", g_theme.accent, ImGuiColorEditFlags_NoInputs)) ApplyStyle();
    RowLabel("HUD background", 200);
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("##hudbg", &g_theme.hudOpacity, 0.0f, 1.0f, "%.2f");
    RowLabel("Notifications", 54);
    ToggleSwitch("##notify", &g_theme.notifications);
    RowLabel("HUD layout", 160);
    if (ImGui::Button("Edit HUD layout", ImVec2(160, 0))) {
        s_hudEdit = true;
        s_menuOpen = false;
    }

    ImGui::Spacing();
    SectionTitle("Profiles");
    ImGui::TextDisabled("Active: %s", modules::ActiveProfile().c_str());
    for (const std::string& name : modules::Profiles()) {
        ImGui::PushID(name.c_str());
        const bool active = name == modules::ActiveProfile();
        ImGui::AlignTextToFramePadding();
        if (active) ImGui::TextColored(Accent(), "%s", name.c_str());
        else ImGui::TextUnformatted(name.c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 150);
        if (ImGui::SmallButton("Load") && modules::LoadProfile(name)) {
            ApplyStyle();
            ui::Notify("Profile loaded", name);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(active);
        if (ImGui::SmallButton("Delete")) modules::DeleteProfile(name);
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##profile", "New profile name", s_profileName, sizeof(s_profileName));
    ImGui::SameLine();
    if (ImGui::Button("Save as") && s_profileName[0]) {
        modules::SaveProfile(s_profileName);
        ui::Notify("Profile saved", s_profileName);
        s_profileName[0] = 0;
    }

    ImGui::Spacing();
    SectionTitle("Keys");
    ImGui::Text("Open menu: %s", KeyName(g_config.menuKey));
    ImGui::Text("Quick waypoint: %s", KeyName(g_config.addWaypointKey));
    ImGui::TextDisabled("Change these in %%LOCALAPPDATA%%\\BedrockWaypoints\\config.ini");

    ImGui::Spacing();
    SectionTitle("About");
    ImGui::TextDisabled("Minecraft %s, profile: %s", game::GameVersion().c_str(),
                        g_offsets.name.empty() ? "none" : g_offsets.name.c_str());
    {
        std::lock_guard lock(s_statusMutex);
        if (!s_status.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%s", s_status.c_str());
    }
    if (state.valid) {
        const Vec3 feet = FeetBlock(state);
        ImGui::TextDisabled("Position %.0f %.0f %.0f  %s  %s", feet.x, feet.y, feet.z, DimensionLabel(state.dimension),
                            state.world);
    } else {
        ImGui::TextDisabled("Not in a world");
    }
}

// ---------------------------------------------------------------- menu

bool SidebarButton(const char* label, bool active) {
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 38);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active || hovered) {
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
                          active ? AccentColor(0.18f) : IM_COL32(255, 255, 255, 10), 8);
    }
    if (active) dl->AddRectFilled(ImVec2(p.x, p.y + 9), ImVec2(p.x + 3, p.y + size.y - 9), AccentColor(), 2);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(active ? s_fontBold : ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + 16, p.y + (size.y - ts.y) / 2),
                active ? IM_COL32_WHITE : IM_COL32(170, 172, 185, 255), label);
    return clicked;
}

void DrawMenu(const GameState& state, ImVec2 screen) {
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_menuAlpha);
    ImGui::SetNextWindowPos(ImVec2(screen.x / 2, screen.y / 2), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(900, 580), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(720, 420), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("##clickgui", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);

    // Header
    ImGui::PushFont(s_fontTitle);
    ImGui::TextColored(Accent(), "%s", ui::kClientName);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
    ImGui::TextDisabled("client");
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 270);
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##search", "Search modules...", s_search, sizeof(s_search));
    ImGui::SameLine();
    if (ImGui::Button("X", ImVec2(36, 0))) s_menuOpen = false;
    ImGui::Spacing();

    // Sidebar
    ImGui::BeginChild("sidebar", ImVec2(180, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    struct Entry {
        const char* label;
        Page page;
    };
    static const Entry entries[] = {{"HUD", Page::Hud},
                                    {"Visual", Page::Visual},
                                    {"Utility", Page::Utility},
                                    {"Waypoints", Page::Waypoints},
                                    {"Settings", Page::Settings}};
    for (const auto& e : entries) {
        if (SidebarButton(e.label, s_page == e.page)) {
            s_page = e.page;
            s_search[0] = 0;
        }
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 60);
    ImGui::TextDisabled("Profile");
    ImGui::TextUnformatted(modules::ActiveProfile().c_str());
    ImGui::EndChild();
    ImGui::SameLine();

    // Content
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("content", ImVec2(0, 0));
    ImGui::PopStyleColor();
    if (s_search[0]) {
        DrawModulePage(Category::Hud);
    } else {
        switch (s_page) {
        case Page::Hud: DrawModulePage(Category::Hud); break;
        case Page::Visual: DrawModulePage(Category::Visual); break;
        case Page::Utility: DrawModulePage(Category::Utility); break;
        case Page::Waypoints: DrawWaypointsPage(state); break;
        case Page::Settings: DrawSettingsPage(state); break;
        }
    }
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar();
}

void DrawHudEditor(ImVec2 screen) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(screen);
    ImGui::Begin("##hudedit", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();

    for (Module* m : modules::All()) {
        if (!m->hud || !m->enabled) continue;
        const ImVec2 pos(std::clamp(m->hudPos.x * screen.x, 0.0f, std::max(0.0f, screen.x - m->lastHudSize.x)),
                         std::clamp(m->hudPos.y * screen.y, 0.0f, std::max(0.0f, screen.y - m->lastHudSize.y)));
        const ImVec2 size(std::max(m->lastHudSize.x, 20.0f), std::max(m->lastHudSize.y, 20.0f));
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton(m->name.c_str(), size);
        const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
            m->hudPos.x = std::clamp((pos.x + io.MouseDelta.x) / screen.x, 0.0f, 1.0f);
            m->hudPos.y = std::clamp((pos.y + io.MouseDelta.y) / screen.y, 0.0f, 1.0f);
        }
        if (hovered && io.MouseWheel != 0) m->hudScale = std::clamp(m->hudScale + io.MouseWheel * 0.1f, 0.5f, 2.5f);

        const ImVec2 max(pos.x + size.x, pos.y + size.y);
        dl->AddRect(ImVec2(pos.x - 3, pos.y - 3), ImVec2(max.x + 3, max.y + 3),
                    AccentColor(hovered || active ? 1.0f : 0.45f), 6, 0, hovered ? 2.0f : 1.0f);
        if (hovered || active) {
            dl->AddText(s_fontBold, 16, ImVec2(pos.x, pos.y - 22), IM_COL32_WHITE, m->name.c_str());
        }
    }

    // Instructions bar
    const char* help = "Drag to move  -  Scroll to resize  -  press the menu key or Done to finish";
    const ImVec2 ts = ImGui::CalcTextSize(help);
    const ImVec2 barMin(screen.x / 2 - ts.x / 2 - 80, screen.y - 90), barMax(screen.x / 2 + ts.x / 2 + 80, screen.y - 40);
    dl->AddRectFilled(barMin, barMax, IM_COL32(16, 17, 24, 235), 12);
    dl->AddText(ImVec2(barMin.x + 18, barMin.y + (barMax.y - barMin.y - ts.y) / 2), IM_COL32(220, 222, 235, 255), help);
    ImGui::SetCursorScreenPos(ImVec2(barMax.x - 80, barMin.y + 10));
    ImGui::PushStyleColor(ImGuiCol_Button, Accent(0.85f));
    if (ImGui::Button("Done", ImVec2(64, 30))) {
        s_hudEdit = false;
        modules::SaveProfile(modules::ActiveProfile());
    }
    ImGui::PopStyleColor();
    ImGui::End();
}

void PollKeys(const GameState& state) {
    const bool focused = GetForegroundWindow() == GameWindow();

    const bool menuDown = (GetAsyncKeyState(g_config.menuKey) & 0x8000) != 0;
    if (menuDown && !s_menuKeyWasDown && focused && !s_bindTarget) {
        if (s_hudEdit) {
            s_hudEdit = false;
            s_menuOpen = true;
            modules::SaveProfile(modules::ActiveProfile());
        } else {
            s_menuOpen = !s_menuOpen;
            if (!s_menuOpen) modules::SaveProfile(modules::ActiveProfile());
        }
    }
    s_menuKeyWasDown = menuDown;

    const bool addDown = (GetAsyncKeyState(g_config.addWaypointKey) & 0x8000) != 0;
    if (addDown && !s_addKeyWasDown && focused && !ImGui::GetIO().WantTextInput && !s_bindTarget)
        AddWaypointAtPlayer(state);
    s_addKeyWasDown = addDown;
}

} // namespace

namespace ui {

void Setup() {
    LoadFonts();
    modules::Init();
    ApplyStyle();
}

void Draw() {
    const GameState state = game::State();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const bool wasOpen = s_menuOpen;

    PollKeyBinding();
    PollKeys(state);
    HandleDeath();
    modules::Frame(state, s_menuOpen || s_hudEdit);
    ImGui::GetIO().MouseDrawCursor = s_menuOpen || s_hudEdit;
    s_menuAlpha = Lerp(s_menuAlpha, s_menuOpen ? 1.0f : 0.0f, ImGui::GetIO().DeltaTime * 16);

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    if (state.valid) {
        for (const auto& w : g_waypoints)
            if (w.visible) DrawWaypoint(bg, state, w, screen.x, screen.y);
    }
    modules::DrawWorld(bg, state, screen);
    if (s_hudEdit) bg->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(0, 0, 0, 90));
    modules::hudPreview = s_hudEdit;
    modules::DrawHud(bg, state, screen);

    if (s_menuOpen) DrawMenu(state, screen);
    if (s_hudEdit) DrawHudEditor(screen);
    if (wasOpen && !s_menuOpen && !s_hudEdit) modules::SaveProfile(modules::ActiveProfile());
    DrawNotifications(screen);
}

bool MenuOpen() {
    return s_menuOpen || s_hudEdit;
}

void SetStatus(const std::string& status) {
    std::lock_guard lock(s_statusMutex);
    s_status = status;
}

void Notify(const std::string& title, const std::string& text) {
    if (!g_theme.notifications) return;
    s_notifications.push_back({title, text, ImGui::GetTime()});
    if (s_notifications.size() > 5) s_notifications.pop_front();
}

ImFont* BoldFont() {
    return s_fontBold;
}

} // namespace ui
