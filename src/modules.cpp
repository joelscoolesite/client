#include "modules.h"

#include "config.h"
#include "input.h"
#include "render.h"
#include "ui.h"
#include "waypoints.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>

Theme g_theme;

namespace modules {
bool hudPreview = false;
}

namespace {

bool s_menuOpen = false;
const ULONGLONG s_injectTick = GetTickCount64();

HWND GameWindow() {
    return static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
}

bool GameFocused() {
    return GetForegroundWindow() == GameWindow();
}

bool KeyDown(int vk) {
    return vk && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// True on the frame a key goes down.
bool KeyPressed(int vk, bool& wasDown) {
    const bool down = KeyDown(vk);
    const bool pressed = down && !wasDown;
    wasDown = down;
    return pressed && GameFocused();
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

const char* FacingName(float heading) {
    static const char* names[] = {"North (-Z)", "North-East", "East (+X)", "South-East",
                                  "South (+Z)", "South-West", "West (-X)", "North-West"};
    return names[static_cast<int>((heading + 22.5f) / 45.0f) % 8];
}

Vec3 PlayerEye(const GameState& s) {
    return s.hasPlayer ? s.playerPos : s.origin;
}

// Label in accent colour followed by a white value, on one line. Returns the line size.
ImVec2 LabelValue(ImDrawList* dl, ImVec2 pos, const char* label, const char* value, float scale) {
    ImFont* bold = ui::BoldFont();
    const ImVec2 labelSize = HudTextSize(label, scale, bold);
    const float gap = 6 * scale;
    if (dl) {
        HudText(dl, pos, AccentColor(), label, scale, bold);
        HudText(dl, ImVec2(pos.x + labelSize.x + gap, pos.y), IM_COL32_WHITE, value, scale);
    }
    const ImVec2 valueSize = HudTextSize(value, scale);
    return {labelSize.x + gap + valueSize.x, std::max(labelSize.y, valueSize.y)};
}

// Panel with one label/value row per entry.
ImVec2 RowsPanel(ImDrawList* dl, ImVec2 pos, const std::vector<std::pair<std::string, std::string>>& rows,
                 float scale) {
    const float pad = 8 * scale, lineGap = 3 * scale;
    ImVec2 size{0, 0};
    for (const auto& [label, value] : rows) {
        const ImVec2 line = LabelValue(nullptr, {}, label.c_str(), value.c_str(), scale);
        size.x = std::max(size.x, line.x);
        size.y += line.y + lineGap;
    }
    size = {size.x + pad * 2, size.y - lineGap + pad * 2};
    HudPanel(dl, pos, ImVec2(pos.x + size.x, pos.y + size.y), scale);
    float y = pos.y + pad;
    for (const auto& [label, value] : rows) {
        const ImVec2 line = LabelValue(dl, ImVec2(pos.x + pad, y), label.c_str(), value.c_str(), scale);
        y += line.y + lineGap;
    }
    return size;
}

// ---------------------------------------------------------------- world drawing

void ToClip(const GameState& s, const Vec3& p, float out[4]) {
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
}

// Draws a 3D line, cutting off the part behind the camera.
void WorldLine(ImDrawList* dl, const GameState& s, Vec3 a, Vec3 b, ImU32 color, float thickness, ImVec2 screen) {
    float ca[4], cb[4];
    ToClip(s, a, ca);
    ToClip(s, b, cb);
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

// ---------------------------------------------------------------- HUD modules

class Coordinates : public Module {
public:
    Coordinates() : Module("Coordinates", "Your position, nether coordinates and facing.", Category::Hud, true) {
        hudPos = {0.005f, 0.01f};
        AddBool("nether", "Show other dimension", nether);
        AddBool("facing", "Show facing", facing);
    }

    ImVec2 DrawHud(ImDrawList* dl, ImVec2 pos, const GameState& s) override {
        char xyz[64] = "100 64 -200", other[64] = "12 64 -25";
        const char* otherLabel = "Nether";
        const Vec3 eye = PlayerEye(s);
        const float x = std::floor(eye.x), y = std::floor(eye.y - kEyeHeight), z = std::floor(eye.z);
        const bool live = s.valid;
        if (live) {
            snprintf(xyz, sizeof(xyz), "%.0f %.0f %.0f", x, y, z);
            if (std::string(s.dimension) == "Nether") {
                otherLabel = "Overworld";
                snprintf(other, sizeof(other), "%.0f %.0f %.0f", x * 8, y, z * 8);
            } else {
                snprintf(other, sizeof(other), "%.0f %.0f %.0f", std::floor(x / 8), y, std::floor(z / 8));
            }
        } else if (!modules::hudPreview) {
            return {};
        }

        std::vector<std::pair<std::string, std::string>> rows{{"XYZ", xyz}};
        const std::string dim = s.dimension;
        if (nether && (dim == "Overworld" || dim == "Nether" || modules::hudPreview)) rows.push_back({otherLabel, other});
        if (facing) rows.push_back({"Facing", live ? FacingName(game::Heading(s)) : "North (-Z)"});
        return RowsPanel(dl, pos, rows, hudScale);
    }

private:
    bool nether = true;
    bool facing = true;
};

class Compass : public Module {
public:
    Compass() : Module("Compass", "Compass bar with your waypoints on it.", Category::Hud, true) {
        hudPos = {0.36f, 0.01f};
        AddInt("width", "Width", width, 200, 700);
        AddBool("waypoints", "Show waypoints", showWaypoints);
    }

    ImVec2 DrawHud(ImDrawList* dl, ImVec2 pos, const GameState& s) override {
        if (!s.valid && !modules::hudPreview) return {};
        const float scale = hudScale;
        const ImVec2 size(width * scale, 30 * scale);
        HudPanel(dl, pos, ImVec2(pos.x + size.x, pos.y + size.y), scale);

        const float heading = s.valid ? game::Heading(s) : 0.0f;
        const float centerX = pos.x + size.x / 2;
        const float range = 90.0f; // degrees visible on each side
        auto xFor = [&](float diff) { return centerX + diff / range * (size.x / 2 - 10 * scale); };
        auto wrap = [](float d) {
            while (d > 180) d -= 360;
            while (d < -180) d += 360;
            return d;
        };

        static const char* labels[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        dl->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
        for (int a = 0; a < 360; a += 15) {
            const float diff = wrap(static_cast<float>(a) - heading);
            if (std::fabs(diff) > range) continue;
            const float x = xFor(diff);
            if (a % 45 == 0) {
                const char* text = labels[a / 45];
                const ImVec2 ts = HudTextSize(text, scale, ui::BoldFont());
                const ImU32 col = a == 0 ? AccentColor() : IM_COL32_WHITE;
                HudText(dl, ImVec2(x - ts.x / 2, pos.y + 4 * scale), col, text, scale, ui::BoldFont());
            } else {
                dl->AddLine(ImVec2(x, pos.y + size.y - 9 * scale), ImVec2(x, pos.y + size.y - 4 * scale),
                            IM_COL32(255, 255, 255, 120), 1.5f * scale);
            }
        }

        if (showWaypoints && s.valid) {
            const Vec3 eye = PlayerEye(s);
            for (const auto& w : g_waypoints) {
                Vec3 p;
                bool converted;
                if (!w.visible || !WaypointDisplayPosition(w, s, p, converted)) continue;
                const float bearing = std::atan2(p.x + 0.5f - eye.x, -(p.z + 0.5f - eye.z)) * 57.29578f;
                const float diff = std::clamp(wrap(bearing - heading), -range, range);
                const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(w.color[0], w.color[1], w.color[2], 1));
                const ImVec2 c(xFor(diff), pos.y + size.y - 6 * scale);
                dl->AddCircleFilled(c, 3.5f * scale, col);
            }
        }
        dl->PopClipRect();

        // Centre marker and heading in degrees.
        dl->AddTriangleFilled(ImVec2(centerX - 5 * scale, pos.y + size.y + 6 * scale),
                              ImVec2(centerX + 5 * scale, pos.y + size.y + 6 * scale),
                              ImVec2(centerX, pos.y + size.y + 1 * scale), AccentColor());
        char deg[16];
        snprintf(deg, sizeof(deg), "%.0f", heading);
        const ImVec2 ds = HudTextSize(deg, scale * 0.85f);
        HudText(dl, ImVec2(centerX - ds.x / 2, pos.y + size.y + 7 * scale), IM_COL32_WHITE, deg, scale * 0.85f);
        return {size.x, size.y + 8 * scale + ds.y};
    }

private:
    int width = 360;
    bool showWaypoints = true;
};

class Fps : public Module {
public:
    Fps() : Module("FPS", "Frames per second.", Category::Hud, true) { hudPos = {0.005f, 0.13f}; }

    ImVec2 DrawHud(ImDrawList* dl, ImVec2 pos, const GameState&) override {
        char value[16];
        snprintf(value, sizeof(value), "%.0f", ImGui::GetIO().Framerate);
        return RowsPanel(dl, pos, {{"FPS", value}}, hudScale);
    }
};

class Cps : public Module {
public:
    Cps() : Module("CPS", "Clicks per second (left | right).", Category::Hud, true) { hudPos = {0.005f, 0.18f}; }

    ImVec2 DrawHud(ImDrawList* dl, ImVec2 pos, const GameState&) override {
        char value[24];
        snprintf(value, sizeof(value), "%d | %d", input::Cps(false), input::Cps(true));
        return RowsPanel(dl, pos, {{"CPS", value}}, hudScale);
    }
};

class Clock : public Module {
public:
    Clock() : Module("Clock", "Time and how long you've been playing.", Category::Hud, true) {
        hudPos = {0.005f, 0.23f};
        AddBool("24h", "24-hour clock", use24h);
        AddBool("session", "Session timer", session);
    }

    ImVec2 DrawHud(ImDrawList* dl, ImVec2 pos, const GameState&) override {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        char time[16];
        if (use24h) {
            snprintf(time, sizeof(time), "%02d:%02d", local.tm_hour, local.tm_min);
        } else {
            const int h = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
            snprintf(time, sizeof(time), "%d:%02d %s", h, local.tm_min, local.tm_hour < 12 ? "AM" : "PM");
        }
        std::vector<std::pair<std::string, std::string>> rows{{"Time", time}};
        if (session) {
            const ULONGLONG secs = (GetTickCount64() - s_injectTick) / 1000;
            char played[24];
            snprintf(played, sizeof(played), "%llu:%02llu:%02llu", secs / 3600, secs / 60 % 60, secs % 60);
            rows.push_back({"Session", played});
        }
        return RowsPanel(dl, pos, rows, hudScale);
    }

private:
    bool use24h = true;
    bool session = true;
};

class Keystrokes : public Module {
public:
    Keystrokes() : Module("Keystrokes", "Shows WASD, mouse and space.", Category::Hud, true) {
        hudPos = {0.005f, 0.5f};
        AddBool("mouse", "Mouse buttons", mouse);
        AddBool("space", "Space bar", space);
    }

    ImVec2 DrawHud(ImDrawList* dl, ImVec2 pos, const GameState&) override {
        const float s = hudScale, k = 38 * s, gap = 4 * s;
        const float width = k * 3 + gap * 2;
        float y = pos.y;
        auto key = [&](ImVec2 min, ImVec2 max, const char* label, bool down, const char* sub = nullptr) {
            float& anim = glow[label];
            anim = Lerp(anim, down ? 1.0f : 0.0f, ImGui::GetIO().DeltaTime * 18);
            const ImVec4 a(g_theme.accent[0], g_theme.accent[1], g_theme.accent[2], 0.35f + 0.6f * anim);
            dl->AddRectFilled(min, max, IM_COL32(12, 12, 18, static_cast<int>(255 * g_theme.hudOpacity)), 6 * s);
            dl->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, a.w * anim)), 6 * s);
            const ImVec2 ts = HudTextSize(label, s, ui::BoldFont());
            const float ty = sub ? min.y + (max.y - min.y) / 2 - ts.y + 2 * s : min.y + (max.y - min.y - ts.y) / 2;
            HudText(dl, ImVec2(min.x + (max.x - min.x - ts.x) / 2, ty), IM_COL32_WHITE, label, s, ui::BoldFont());
            if (sub) {
                const ImVec2 ss = HudTextSize(sub, s * 0.8f);
                HudText(dl, ImVec2(min.x + (max.x - min.x - ss.x) / 2, ty + ts.y), IM_COL32(220, 220, 230, 255), sub,
                        s * 0.8f);
            }
        };

        key(ImVec2(pos.x + k + gap, y), ImVec2(pos.x + 2 * k + gap, y + k), "W", KeyDown('W'));
        y += k + gap;
        key(ImVec2(pos.x, y), ImVec2(pos.x + k, y + k), "A", KeyDown('A'));
        key(ImVec2(pos.x + k + gap, y), ImVec2(pos.x + 2 * k + gap, y + k), "S", KeyDown('S'));
        key(ImVec2(pos.x + 2 * (k + gap), y), ImVec2(pos.x + width, y + k), "D", KeyDown('D'));
        y += k + gap;
        if (mouse) {
            char l[16], r[16];
            snprintf(l, sizeof(l), "%d CPS", input::Cps(false));
            snprintf(r, sizeof(r), "%d CPS", input::Cps(true));
            const float half = (width - gap) / 2;
            key(ImVec2(pos.x, y), ImVec2(pos.x + half, y + k), "LMB", KeyDown(VK_LBUTTON), l);
            key(ImVec2(pos.x + half + gap, y), ImVec2(pos.x + width, y + k), "RMB", KeyDown(VK_RBUTTON), r);
            y += k + gap;
        }
        if (space) {
            key(ImVec2(pos.x, y), ImVec2(pos.x + width, y + k * 0.55f), "_____", KeyDown(VK_SPACE));
            y += k * 0.55f + gap;
        }
        return {width, y - pos.y - gap};
    }

private:
    bool mouse = true;
    bool space = true;
    std::map<std::string, float> glow;
};

// ---------------------------------------------------------------- Visual

class ChunkBorders : public Module {
public:
    ChunkBorders() : Module("Chunk Borders", "Shows the edges of the chunk you're in.", Category::Visual) {
        AddColor("color", "Current chunk", color);
        AddColor("neighbour", "Neighbour chunks", neighbour);
    }

    void OnWorld(ImDrawList* dl, const GameState& s, ImVec2 screen) override {
        const Vec3 eye = PlayerEye(s);
        const float cx = std::floor(eye.x / 16) * 16, cz = std::floor(eye.z / 16) * 16;
        const float y0 = std::floor(eye.y) - 24, y1 = std::floor(eye.y) + 24;
        const ImU32 main = ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], color[3]));
        const ImU32 faint = ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], color[3] * 0.35f));
        const ImU32 other = ImGui::ColorConvertFloat4ToU32(ImVec4(neighbour[0], neighbour[1], neighbour[2], neighbour[3]));

        for (int i = -1; i <= 2; ++i) {
            for (int j = -1; j <= 2; ++j) {
                const bool inner = (i == 0 || i == 1) && (j == 0 || j == 1);
                const float x = cx + i * 16, z = cz + j * 16;
                WorldLine(dl, s, {x, y0, z}, {x, y1, z}, inner ? main : other, inner ? 2.0f : 1.5f, screen);
            }
        }
        // Rings around the current chunk every 4 blocks of height.
        for (float y = y0; y <= y1; y += 4) {
            const ImU32 c = std::fmod(y, 16.0f) == 0 ? main : faint;
            WorldLine(dl, s, {cx, y, cz}, {cx + 16, y, cz}, c, 1.0f, screen);
            WorldLine(dl, s, {cx + 16, y, cz}, {cx + 16, y, cz + 16}, c, 1.0f, screen);
            WorldLine(dl, s, {cx + 16, y, cz + 16}, {cx, y, cz + 16}, c, 1.0f, screen);
            WorldLine(dl, s, {cx, y, cz + 16}, {cx, y, cz}, c, 1.0f, screen);
        }
    }

private:
    float color[4] = {1.0f, 0.85f, 0.2f, 0.9f};
    float neighbour[4] = {1.0f, 0.3f, 0.3f, 0.6f};
};

class Fullbright : public Module {
public:
    Fullbright() : Module("Fullbright", "See everything, even in caves.", Category::Visual) {
        AddFloat("gamma", "Brightness", gamma, 1, 25, "%.0f");
    }
    bool Available() const override { return game::GammaAvailable(); }
    void OnFrame(const GameState&) override { game::SetGamma(gamma); }
    void OnToggle(bool on) override {
        if (!on) game::SetGamma(-1);
    }

private:
    float gamma = 12;
};

class Zoom : public Module {
public:
    Zoom() : Module("Zoom", "Hold the zoom key to zoom in.", Category::Visual) {
        AddKey("key", "Zoom key", zoomKey);
        AddFloat("factor", "Zoom", factor, 1.5f, 15, "%.1fx");
        AddBool("smooth", "Smooth", smooth);
    }
    bool Available() const override { return game::ZoomAvailable(); }

    void OnFrame(const GameState&) override {
        const bool want = KeyDown(zoomKey) && GameFocused() && !s_menuOpen;
        const float target = want ? factor : 1.0f;
        current = smooth ? Lerp(current, target, ImGui::GetIO().DeltaTime * 14) : target;
        game::SetZoom(current);
    }
    void OnToggle(bool on) override {
        if (!on) {
            current = 1;
            game::SetZoom(1);
        }
    }

private:
    int zoomKey = 'C';
    float factor = 4;
    bool smooth = true;
    float current = 1;
};

// ---------------------------------------------------------------- Utility

class ToggleSprintSneak : public Module {
public:
    ToggleSprintSneak()
        : Module("Toggle Sprint", "Tap sprint or sneak once to keep it on (experimental).", Category::Utility, true) {
        hudPos = {0.005f, 0.93f};
        AddBool("sprint", "Toggle sprint", sprint);
        AddKey("sprintKey", "Sprint key", sprintKey);
        AddBool("sneak", "Toggle sneak", sneak);
        AddKey("sneakKey", "Sneak key", sneakKey);
    }

    void OnFrame(const GameState&) override {
        input::toggleSprintEnabled = sprint;
        input::toggleSneakEnabled = sneak;
        input::sprintKey = sprintKey;
        input::sneakKey = sneakKey;
    }
    void OnToggle(bool on) override {
        if (on) return;
        input::toggleSprintEnabled = false;
        input::toggleSneakEnabled = false;
        input::ReleaseToggles(GameWindow());
    }

    ImVec2 DrawHud(ImDrawList* dl, ImVec2 pos, const GameState&) override {
        const char* text = input::SneakToggled() ? "Sneaking (Toggled)"
                           : input::SprintToggled() ? "Sprinting (Toggled)"
                           : modules::hudPreview    ? "Sprinting (Toggled)"
                                                    : nullptr;
        if (!text) return {};
        const float pad = 6 * hudScale;
        const ImVec2 ts = HudTextSize(text, hudScale);
        const ImVec2 size(ts.x + pad * 2, ts.y + pad * 2);
        HudPanel(dl, pos, ImVec2(pos.x + size.x, pos.y + size.y), hudScale);
        HudText(dl, ImVec2(pos.x + pad, pos.y + pad), IM_COL32_WHITE, text, hudScale);
        return size;
    }

private:
    bool sprint = true;
    bool sneak = false;
    int sprintKey = VK_CONTROL;
    int sneakKey = VK_SHIFT;
};

class ChatCoordinates : public Module {
public:
    ChatCoordinates()
        : Module("Chat Coordinates", "Spots coordinates in chat so you can save them as a waypoint.",
                 Category::Utility) {
        AddBool("autoAdd", "Add as waypoint automatically", autoAdd);
    }
    bool Available() const override { return game::ChatAvailable(); }

    void OnFrame(const GameState& s) override {
        ChatCoords found;
        while (game::PollChatCoords(found)) {
            char coords[64];
            snprintf(coords, sizeof(coords), "%.0f %.0f %.0f", found.pos.x, found.pos.y, found.pos.z);
            if (autoAdd) {
                Waypoint w;
                w.name = "Chat";
                w.pos = found.pos;
                w.world = s.world;
                w.dimension = s.dimension;
                w.color[0] = 1.0f;
                w.color[1] = 0.85f;
                w.color[2] = 0.3f;
                g_waypoints.push_back(w);
                SaveWaypoints();
                ui::Notify("Waypoint from chat", coords);
            } else {
                g_chatFound.push_back(found);
                if (g_chatFound.size() > 8) g_chatFound.erase(g_chatFound.begin());
                ui::Notify("Coordinates in chat", std::string(coords) + "  -  add them in Waypoints");
            }
        }
    }

private:
    bool autoAdd = false;
};

class Screenshot : public Module {
public:
    Screenshot() : Module("Screenshot", "Press the key to copy the game screen to your clipboard.", Category::Utility) {
        AddKey("key", "Screenshot key", shotKey);
    }
    void OnFrame(const GameState&) override {
        if (KeyPressed(shotKey, wasDown) && !s_menuOpen) render::RequestScreenshot();
    }

private:
    int shotKey = VK_F6;
    bool wasDown = false;
};

std::vector<std::unique_ptr<Module>> s_owned;
std::vector<Module*> s_modules;
std::string s_activeProfile = "Default";

std::filesystem::path ProfileDir() {
    auto dir = DataDir() / L"profiles";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// Profile names become file names, so keep them to safe ASCII characters.
std::string CleanName(const std::string& name) {
    std::string out;
    for (char ch : name)
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '-' || ch == '_') out += ch;
    return out.empty() ? "Default" : out;
}

std::filesystem::path ProfilePath(const std::string& name) {
    return ProfileDir() / (CleanName(name) + ".ini");
}

void SaveActiveName() {
    std::ofstream(ProfileDir() / L"active.txt") << s_activeProfile;
}

} // namespace

// ---------------------------------------------------------------- Module

Module::Module(std::string name_, std::string description_, Category category_, bool hud_)
    : name(std::move(name_)), description(std::move(description_)), category(category_), hud(hud_) {}

void Module::SetEnabled(bool on) {
    if (enabled == on) return;
    enabled = on;
    OnToggle(on);
}

void Module::AddBool(const char* id, const char* label, bool& value) {
    settings.push_back({id, label, Setting::Type::Bool, &value});
}
void Module::AddFloat(const char* id, const char* label, float& value, float min, float max, const char* format) {
    settings.push_back({id, label, Setting::Type::Float, &value, min, max, format});
}
void Module::AddInt(const char* id, const char* label, int& value, int min, int max) {
    settings.push_back({id, label, Setting::Type::Int, &value, static_cast<float>(min), static_cast<float>(max)});
}
void Module::AddKey(const char* id, const char* label, int& value) {
    settings.push_back({id, label, Setting::Type::Key, &value});
}
void Module::AddColor(const char* id, const char* label, float (&value)[4]) {
    settings.push_back({id, label, Setting::Type::Color, value});
}

// ---------------------------------------------------------------- drawing helpers

ImU32 AccentColor(float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(g_theme.accent[0], g_theme.accent[1], g_theme.accent[2], alpha));
}

void HudPanel(ImDrawList* dl, ImVec2 min, ImVec2 max, float scale) {
    dl->AddRectFilled(min, max, IM_COL32(12, 12, 18, static_cast<int>(255 * g_theme.hudOpacity)), 7 * scale);
    dl->AddRectFilled(ImVec2(min.x, min.y + 5 * scale), ImVec2(min.x + 2.5f * scale, max.y - 5 * scale),
                      AccentColor(), 2 * scale);
}

ImVec2 HudTextSize(const char* text, float scale, ImFont* font) {
    if (!font) font = ImGui::GetFont();
    return font->CalcTextSizeA(ImGui::GetFontSize() * scale, FLT_MAX, 0, text);
}

void HudText(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text, float scale, ImFont* font) {
    if (!font) font = ImGui::GetFont();
    const float size = ImGui::GetFontSize() * scale;
    dl->AddText(font, size, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 140), text);
    dl->AddText(font, size, pos, color, text);
}

// ---------------------------------------------------------------- registry

namespace modules {

void Init() {
    s_owned.push_back(std::make_unique<Coordinates>());
    s_owned.push_back(std::make_unique<Compass>());
    s_owned.push_back(std::make_unique<Fps>());
    s_owned.push_back(std::make_unique<Cps>());
    s_owned.push_back(std::make_unique<Clock>());
    s_owned.push_back(std::make_unique<Keystrokes>());
    s_owned.push_back(std::make_unique<ChunkBorders>());
    s_owned.push_back(std::make_unique<Fullbright>());
    s_owned.push_back(std::make_unique<Zoom>());
    s_owned.push_back(std::make_unique<ToggleSprintSneak>());
    s_owned.push_back(std::make_unique<ChatCoordinates>());
    s_owned.push_back(std::make_unique<Screenshot>());
    for (auto& m : s_owned) s_modules.push_back(m.get());

    // Sensible first-run defaults.
    for (const char* on : {"Coordinates", "Compass", "FPS", "Zoom", "Chat Coordinates", "Screenshot"})
        for (Module* m : s_modules)
            if (m->name == on) m->enabled = true;

    std::ifstream active(ProfileDir() / L"active.txt");
    std::string name;
    if (std::getline(active, name) && !name.empty()) s_activeProfile = name;
    if (!LoadProfile(s_activeProfile)) SaveProfile(s_activeProfile);
}

std::vector<Module*>& All() {
    return s_modules;
}

void Frame(const GameState& state, bool menuOpen) {
    s_menuOpen = menuOpen;
    const bool typing = ImGui::GetIO().WantTextInput;
    for (Module* m : s_modules) {
        if (m->key && !menuOpen && !typing && KeyPressed(m->key, m->keyWasDown)) {
            m->SetEnabled(!m->enabled);
            ui::Notify(m->name, m->enabled ? "Enabled" : "Disabled");
        }
        if (m->enabled && m->Available()) m->OnFrame(state);
    }
}

void DrawWorld(ImDrawList* dl, const GameState& state, ImVec2 screen) {
    if (!state.valid) return;
    for (Module* m : s_modules)
        if (m->enabled && m->Available()) m->OnWorld(dl, state, screen);
}

void DrawHud(ImDrawList* dl, const GameState& state, ImVec2 screen) {
    for (Module* m : s_modules) {
        if (!m->hud || !m->enabled) continue;
        ImVec2 pos(m->hudPos.x * screen.x, m->hudPos.y * screen.y);
        // Keep elements on screen after a resize.
        pos.x = std::clamp(pos.x, 0.0f, std::max(0.0f, screen.x - m->lastHudSize.x));
        pos.y = std::clamp(pos.y, 0.0f, std::max(0.0f, screen.y - m->lastHudSize.y));
        m->lastHudSize = m->DrawHud(dl, pos, state);
    }
}

std::string ActiveProfile() {
    return s_activeProfile;
}

void SaveProfile(const std::string& name) {
    std::ofstream f(ProfilePath(name), std::ios::trunc);
    f << "theme.accent=" << g_theme.accent[0] << ',' << g_theme.accent[1] << ',' << g_theme.accent[2] << '\n'
      << "theme.hudOpacity=" << g_theme.hudOpacity << '\n'
      << "theme.notifications=" << g_theme.notifications << '\n';
    for (Module* m : s_modules) {
        const std::string p = m->name + ".";
        f << p << "enabled=" << m->enabled << '\n'
          << p << "key=" << m->key << '\n'
          << p << "hudX=" << m->hudPos.x << '\n'
          << p << "hudY=" << m->hudPos.y << '\n'
          << p << "hudScale=" << m->hudScale << '\n';
        for (const Setting& st : m->settings) {
            f << p << st.id << '=';
            switch (st.type) {
            case Setting::Type::Bool: f << *static_cast<bool*>(st.value); break;
            case Setting::Type::Float: f << *static_cast<float*>(st.value); break;
            case Setting::Type::Int:
            case Setting::Type::Key: f << *static_cast<int*>(st.value); break;
            case Setting::Type::Color: {
                const float* c = static_cast<float*>(st.value);
                f << c[0] << ',' << c[1] << ',' << c[2] << ',' << c[3];
                break;
            }
            }
            f << '\n';
        }
    }
    s_activeProfile = CleanName(name);
    SaveActiveName();
}

bool LoadProfile(const std::string& name) {
    std::ifstream f(ProfilePath(name));
    if (!f) return false;
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq != std::string::npos) values[line.substr(0, eq)] = line.substr(eq + 1);
    }
    auto num = [](const std::string& v) { return std::strtof(v.c_str(), nullptr); };
    auto floats = [&](const std::string& v, float* out, int count) {
        const char* p = v.c_str();
        for (int i = 0; i < count && *p; ++i) {
            char* end = nullptr;
            out[i] = std::strtof(p, &end);
            p = *end == ',' ? end + 1 : end;
        }
    };

    if (values.count("theme.accent")) floats(values["theme.accent"], g_theme.accent, 3);
    if (values.count("theme.hudOpacity")) g_theme.hudOpacity = num(values["theme.hudOpacity"]);
    if (values.count("theme.notifications")) g_theme.notifications = values["theme.notifications"] == "1";

    for (Module* m : s_modules) {
        const std::string p = m->name + ".";
        auto get = [&](const std::string& key, std::string& out) {
            const auto it = values.find(p + key);
            if (it == values.end()) return false;
            out = it->second;
            return true;
        };
        std::string v;
        if (get("enabled", v)) m->SetEnabled(v == "1");
        if (get("key", v)) m->key = static_cast<int>(num(v));
        if (get("hudX", v)) m->hudPos.x = num(v);
        if (get("hudY", v)) m->hudPos.y = num(v);
        if (get("hudScale", v)) m->hudScale = std::clamp(num(v), 0.5f, 3.0f);
        for (const Setting& st : m->settings) {
            if (!get(st.id, v)) continue;
            switch (st.type) {
            case Setting::Type::Bool: *static_cast<bool*>(st.value) = v == "1"; break;
            case Setting::Type::Float: *static_cast<float*>(st.value) = num(v); break;
            case Setting::Type::Int:
            case Setting::Type::Key: *static_cast<int*>(st.value) = static_cast<int>(num(v)); break;
            case Setting::Type::Color: floats(v, static_cast<float*>(st.value), 4); break;
            }
        }
    }
    s_activeProfile = CleanName(name);
    SaveActiveName();
    return true;
}

void DeleteProfile(const std::string& name) {
    std::error_code ec;
    std::filesystem::remove(ProfilePath(name), ec);
}

std::vector<std::string> Profiles() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(ProfileDir(), ec)) {
        if (entry.path().extension() == ".ini") names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace modules
