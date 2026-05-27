#ifdef _WIN32

#include "platform/IHotkeyService.h"

#include <cstdio>
#include <SDL.h>
#include <SDL_syswm.h>
#include <Windows.h>

namespace {

constexpr int kToggleHotkeyId = 0xB10C;  // arbitrary cookie value

class WindowsHotkeyService : public IHotkeyService {
public:
    explicit WindowsHotkeyService(SDL_Window* w) : window(w) {
        // Route Win32 messages through SDL_PollEvent as SDL_SYSWMEVENT so we
        // can intercept WM_HOTKEY without owning the message loop.
        SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
    }

    ~WindowsHotkeyService() override {
        if (registered && hwnd) {
            UnregisterHotKey(hwnd, kToggleHotkeyId);
        }
    }

    bool registerToggle(Callback cb) override {
        toggleCb = std::move(cb);

        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS) {
            std::fprintf(stderr, "[WinHotkey] SDL_GetWindowWMInfo failed\n");
            return false;
        }
        hwnd = info.info.win.window;

        // Default: Ctrl+Alt+H. Bypasses common conflicts (Win+H is dictation).
        if (!RegisterHotKey(hwnd, kToggleHotkeyId, MOD_CONTROL | MOD_ALT, 'H')) {
            std::fprintf(stderr,
                "[WinHotkey] RegisterHotKey failed (GetLastError=%lu)\n",
                GetLastError());
            return false;
        }
        registered = true;
        std::fprintf(stderr, "[WinHotkey] Toggle hotkey registered: Ctrl+Alt+H\n");
        return true;
    }

    void handleSDLSysWMEvent(const SDL_Event& ev) override {
        if (ev.type != SDL_SYSWMEVENT) return;
        const SDL_SysWMmsg* msg = ev.syswm.msg;
        if (!msg || msg->subsystem != SDL_SYSWM_WINDOWS) return;
        if (msg->msg.win.msg == WM_HOTKEY && (int)msg->msg.win.wParam == kToggleHotkeyId) {
            if (toggleCb) toggleCb();
        }
    }

private:
    SDL_Window* window = nullptr;
    HWND hwnd = nullptr;
    bool registered = false;
    Callback toggleCb;
};

}  // namespace

std::unique_ptr<IHotkeyService> IHotkeyService::create(SDL_Window* window) {
    return std::make_unique<WindowsHotkeyService>(window);
}

#endif  // _WIN32
