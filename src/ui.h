#pragma once
#include <string>

struct ImFont;

namespace ui {

constexpr const char* kClientName = "Nova";

void Setup();
// Called every frame between ImGui::NewFrame() and ImGui::Render().
void Draw();
// True while the menu or the HUD editor is open (game input is blocked then).
bool MenuOpen();
// Error shown in the menu (e.g. signature not found).
void SetStatus(const std::string& status);
// Small popup in the bottom-right corner. Render thread only.
void Notify(const std::string& title, const std::string& text);
ImFont* BoldFont();

} // namespace ui
