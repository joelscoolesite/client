#include "input.h"

#include <deque>
#include <mutex>

namespace input {

std::atomic<bool> toggleSprintEnabled{false};
std::atomic<bool> toggleSneakEnabled{false};
std::atomic<int> sprintKey{VK_CONTROL};
std::atomic<int> sneakKey{VK_SHIFT};

namespace {

std::mutex s_clickMutex;
std::deque<ULONGLONG> s_clicks[2];

std::atomic<bool> s_sprintHeld{false};
std::atomic<bool> s_sneakHeld{false};

void AddClick(int button) {
    std::lock_guard lock(s_clickMutex);
    s_clicks[button].push_back(GetTickCount64());
    if (s_clicks[button].size() > 100) s_clicks[button].pop_front();
}

LPARAM KeyUpParam(WPARAM key) {
    const UINT scan = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
    return static_cast<LPARAM>(1 | (scan << 16) | (1u << 30) | (1u << 31));
}

// Pressing the key flips the toggle. While toggled, the key-up is hidden so the game thinks it is still held.
bool HandleToggle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC original, bool enabled, int key,
                  std::atomic<bool>& held) {
    if (!enabled || static_cast<int>(wParam) != key) return false;
    if (msg == WM_KEYDOWN) {
        if (lParam & (1 << 30)) return held.load(); // auto-repeat
        held = !held.load();
        if (held) return false; // let the press through
        CallWindowProcW(original, hwnd, WM_KEYUP, wParam, KeyUpParam(wParam));
        return true;
    }
    if (msg == WM_KEYUP) return held.load();
    return false;
}

} // namespace

bool OnMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC original) {
    if (msg == WM_LBUTTONDOWN) AddClick(0);
    if (msg == WM_RBUTTONDOWN) AddClick(1);

    return HandleToggle(hwnd, msg, wParam, lParam, original, toggleSprintEnabled, sprintKey, s_sprintHeld) ||
           HandleToggle(hwnd, msg, wParam, lParam, original, toggleSneakEnabled, sneakKey, s_sneakHeld);
}

int Cps(bool right) {
    std::lock_guard lock(s_clickMutex);
    auto& clicks = s_clicks[right ? 1 : 0];
    const ULONGLONG now = GetTickCount64();
    while (!clicks.empty() && now - clicks.front() > 1000) clicks.pop_front();
    return static_cast<int>(clicks.size());
}

bool SprintToggled() {
    return s_sprintHeld;
}

bool SneakToggled() {
    return s_sneakHeld;
}

void ReleaseToggles(HWND hwnd) {
    if (s_sprintHeld.exchange(false)) PostMessageW(hwnd, WM_KEYUP, sprintKey, KeyUpParam(sprintKey));
    if (s_sneakHeld.exchange(false)) PostMessageW(hwnd, WM_KEYUP, sneakKey, KeyUpParam(sneakKey));
}

} // namespace input
