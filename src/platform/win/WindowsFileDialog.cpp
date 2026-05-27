#ifdef _WIN32

#include "platform/file_dialog.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <Windows.h>
#include <commdlg.h>

namespace platform {

std::string openMidiFileDialog(SDL_Window* parent) {
    HWND owner = nullptr;
    if (parent) {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(parent, &info) && info.subsystem == SDL_SYSWM_WINDOWS) {
            owner = info.info.win.window;
        }
    }

    char filename[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFile   = filename;
    ofn.nMaxFile    = sizeof(filename);
    // Embedded NULs in the filter string separate fields; double-NUL ends.
    ofn.lpstrFilter = "MIDI files (*.mid;*.midi)\0*.mid;*.midi\0All files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle  = "Select a MIDI file";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameA(&ofn)) return {};
    return std::string(filename);
}

}  // namespace platform

#endif  // _WIN32
