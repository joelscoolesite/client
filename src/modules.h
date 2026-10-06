#pragma once
#include "game.h"

#include <imgui.h>

#include <string>
#include <vector>

enum class Category { Hud, Visual, Utility };

struct Setting {
    enum class Type { Bool, Float, Int, Key, Color };
    std::string id;
    std::string label;
    Type type;
    void* value;
    float min = 0;
    float max = 1;
    const char* format = nullptr;
};

class Module {
public:
    Module(std::string name, std::string description, Category category, bool hud = false);
    virtual ~Module() = default;

    virtual void OnFrame(const GameState&) {}                       // every frame while enabled
    virtual void OnWorld(ImDrawList*, const GameState&, ImVec2) {}  // draw in the 3D world while enabled
    virtual ImVec2 DrawHud(ImDrawList*, ImVec2, const GameState&) { // draw at pos, return size
        return {};
    }
    virtual void OnToggle(bool) {}
    virtual bool Available() const { return true; } // false when this Minecraft version lacks the offsets

    void SetEnabled(bool on);

    std::string name;
    std::string description;
    Category category;
    bool hud;
    bool enabled = false;
    int key = 0;                 // toggle key, 0 = none
    bool keyWasDown = false;
    ImVec2 hudPos{0.01f, 0.3f};  // top-left as a fraction of the screen
    float hudScale = 1.0f;
    ImVec2 lastHudSize{0, 0};    // for the HUD editor
    std::vector<Setting> settings;

protected:
    void AddBool(const char* id, const char* label, bool& value);
    void AddFloat(const char* id, const char* label, float& value, float min, float max, const char* format = "%.1f");
    void AddInt(const char* id, const char* label, int& value, int min, int max);
    void AddKey(const char* id, const char* label, int& value);
    void AddColor(const char* id, const char* label, float (&value)[4]);
};

struct Theme {
    float accent[4] = {0.56f, 0.42f, 1.0f, 1.0f};
    float hudOpacity = 0.55f;
    bool notifications = true;
};
extern Theme g_theme;

ImU32 AccentColor(float alpha = 1.0f);

// Shared look for HUD elements: rounded dark panel and text with a soft shadow.
void HudPanel(ImDrawList* dl, ImVec2 min, ImVec2 max, float scale);
void HudText(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text, float scale, ImFont* font = nullptr);
ImVec2 HudTextSize(const char* text, float scale, ImFont* font = nullptr);

namespace modules {

// True while the HUD editor is open: HUD modules then draw sample content so they can be moved.
extern bool hudPreview;

void Init();
std::vector<Module*>& All();

void Frame(const GameState& state, bool menuOpen); // keybinds + OnFrame
void DrawWorld(ImDrawList* dl, const GameState& state, ImVec2 screen);
void DrawHud(ImDrawList* dl, const GameState& state, ImVec2 screen);

// Profiles live in DataDir()/profiles/<name>.ini
std::string ActiveProfile();
void SaveProfile(const std::string& name);
bool LoadProfile(const std::string& name);
void DeleteProfile(const std::string& name);
std::vector<std::string> Profiles();

} // namespace modules
