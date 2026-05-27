#ifdef __APPLE__

#include "platform/overlay.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <Cocoa/Cocoa.h>

void configureWindowOverlay(SDL_Window* window) {
    if (!window) return;

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) return;
    if (info.subsystem != SDL_SYSWM_COCOA) return;

    NSWindow* nsWin = info.info.cocoa.window;
    if (!nsWin) return;

    // NSStatusWindowLevel sits above normal windows and above most fullscreen
    // apps. NSScreenSaverWindowLevel is higher still but draws over login UI
    // which is overkill.
    [nsWin setLevel:NSStatusWindowLevel];

    // CanJoinAllSpaces: window follows the user across Spaces (incl. fullscreen).
    // FullScreenAuxiliary: explicitly allowed to overlay fullscreen apps.
    // Stationary: don't get pulled along during Mission Control swipes.
    NSWindowCollectionBehavior behavior =
        NSWindowCollectionBehaviorCanJoinAllSpaces |
        NSWindowCollectionBehaviorFullScreenAuxiliary |
        NSWindowCollectionBehaviorStationary;
    [nsWin setCollectionBehavior:behavior];

    // Hide from the application switcher (Cmd+Tab) — overlay is an aux panel,
    // not a "real" window the user alt-tabs to. Comment out if undesirable.
    // [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

#endif
