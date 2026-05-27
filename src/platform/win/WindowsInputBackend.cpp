#ifdef _WIN32

#include "WindowsInputBackend.h"
#include "inpututils.h"   // existing Win32 SendInput implementation
#include "Qwerty.h"

// Forward declarations of the existing extern scan-set selector from
// inpututils.cpp. The qwerty_* functions branch on `scanSetChoice` internally.
extern int scanSetChoice;

WindowsInputBackend::WindowsInputBackend() {
    loadScansets();
}

void WindowsInputBackend::sendKeyDown(char c) {
    if (mode == 0) ::sendKeyDown(c);
    else           ::qwerty_sendKeyDown(c);
}

void WindowsInputBackend::sendKeyUp(char c, char location) {
    if (mode == 0) ::sendKeyUp(c, location);
    else           ::qwerty_sendKeyUp(c, location);
}

void WindowsInputBackend::sendOutOfRangeKey(char c) {
    if (mode == 0) ::sendOutOfRangeKey(c);
    else           ::qwerty_sendOutOfRangeKey(c);
}

void WindowsInputBackend::setVelocity(char c) {
    if (mode == 0) ::setVelocity(c);
    else           ::qwerty_setVelocity(c);
}

std::vector<std::string> WindowsInputBackend::availableModes() const {
    return { "Off (OS-aware)", "Set 1", "Set 2", "QWERTZ" };
}

std::vector<std::string> WindowsInputBackend::modeTooltips() const {
    // Preserved from the original main.cpp Settings combo.
    return {
        "Consults Windows and your keyboard layout,\nuseful for playing outside of Roblox",
        "This should be your go-to setting for Roblox",
        "Use this if your keyboard doesn't\nproperly support Set 1",
        "This is like Set 1, but swaps Y with Z",
    };
}

void WindowsInputBackend::setMode(int modeIndex) {
    mode = modeIndex;
    // Mirror legacy scanSetChoice mapping from setEmulatorFunctions().
    if (modeIndex == 1) scanSetChoice = 0;
    else if (modeIndex == 2) scanSetChoice = 1;
    else if (modeIndex == 3) scanSetChoice = 2;
}

int WindowsInputBackend::currentMode() const {
    return mode;
}

void WindowsInputBackend::releaseAll() {
    // Best-effort: release the modifier keys + space that we synthesise. The
    // per-note up events come from the note router calling sendKeyUp for each
    // note it knows is held, so this is just for "stuck modifier" recovery.
    // Cheaper than tracking every note here.
    sendKeyUp(' ', 'm'); // sustain
}

#endif // _WIN32
