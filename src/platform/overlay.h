#pragma once

struct SDL_Window;

// Configure the platform window so it floats reliably over fullscreen apps
// (read: Roblox). Windows: relies on SDL_WINDOW_ALWAYS_ON_TOP (no-op here).
// macOS: lifts the underlying NSWindow to a high level and adds the join-all-
// spaces / fullscreen-auxiliary collection-behavior bits so it appears on top
// of fullscreen Spaces instead of being trapped under them.
void configureWindowOverlay(SDL_Window* window);
