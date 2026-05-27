#ifdef _WIN32

#include "platform/overlay.h"

// Windows already keeps the window on top via SDL_WINDOW_ALWAYS_ON_TOP set on
// the SDL_CreateWindow call, applied/toggled with SDL_SetWindowAlwaysOnTop.
// If we hit Roblox-fullscreen overlay issues later, escalate to WS_EX_TOPMOST
// via SetWindowPos here.
void configureWindowOverlay(SDL_Window*) {}

#endif
