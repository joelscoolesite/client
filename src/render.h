#pragma once
#include <string>

namespace render {

// Hooks IDXGISwapChain::Present/ResizeBuffers (DX11 + DX12) and draws ImGui on top of the game.
bool Init(std::string& error);

} // namespace render
