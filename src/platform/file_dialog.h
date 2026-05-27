#pragma once
#include <string>

struct SDL_Window;

namespace platform {

// Opens a native file picker filtered to .mid / .midi. Returns the absolute
// path of the selected file, or an empty string if the user cancelled.
// Synchronous (blocks the UI thread while the dialog is up). Must be called
// from the main thread.
std::string openMidiFileDialog(SDL_Window* parent);

}  // namespace platform
