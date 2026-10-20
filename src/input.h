#pragma once
#include <windows.h>

#include <atomic>

namespace input {

// Toggle sprint/sneak: pressing the key once keeps it held for the game until pressed again.
extern std::atomic<bool> toggleSprintEnabled;
extern std::atomic<bool> toggleSneakEnabled;
extern std::atomic<int> sprintKey; // VK_CONTROL by default
extern std::atomic<int> sneakKey;  // VK_SHIFT by default

// Called from the window procedure before anything else.
// Returns true when the message must not reach the game.
bool OnMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC original);

// Clicks in the last second.
int Cps(bool right);

bool SprintToggled();
bool SneakToggled();

// Sends a key press or release to the game window, as if the key was pressed.
void SendKey(HWND hwnd, int vk, bool down);

// Lets go of a toggled key (used when the feature is switched off).
void ReleaseToggles(HWND hwnd);

} // namespace input
