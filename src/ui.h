#pragma once
#include <string>

namespace ui {

void Setup();
// Called every frame between ImGui::NewFrame() and ImGui::Render().
void Draw();
bool MenuOpen();
// Error shown at the top of the menu (e.g. signature not found).
void SetStatus(const std::string& status);

} // namespace ui
