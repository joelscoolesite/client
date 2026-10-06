#include "config.h"
#include "game.h"
#include "render.h"
#include "ui.h"
#include "waypoints.h"

#include <windows.h>
#include <MinHook.h>

#include <string>

static DWORD WINAPI InitThread(LPVOID) {
    LoadConfig();
    LoadWaypoints();
    LoadSettings();
    Log("Injected");

    if (MH_Initialize() != MH_OK) {
        Log("MH_Initialize failed");
        return 0;
    }

    std::string error;
    if (!game::Init(error)) {
        // Keep going: the menu still opens and shows what went wrong.
        Log(error);
        ui::SetStatus(error);
    }
    error.clear();
    if (!render::Init(error)) {
        Log(error);
        return 0;
    }
    Log("Hooks installed, press the menu key in game");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
