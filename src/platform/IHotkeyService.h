#pragma once
#include <functional>
#include <memory>

union SDL_Event;
struct SDL_Window;

// Global keyboard shortcut registration. Implementations:
//   Windows: RegisterHotKey routed through SDL's SDL_SYSWMEVENT pipeline.
//   macOS:   RegisterEventHotKey on GetApplicationEventTarget; events fire on
//            the Cocoa runloop, which is the same thread SDL pumps from.
//
// "Global" means the hotkey fires regardless of which app has focus, including
// when Roblox is the foreground fullscreen app — that is the entire reason
// this exists, so users can toggle the overlay from inside the game.
class IHotkeyService {
public:
    virtual ~IHotkeyService() = default;
    using Callback = std::function<void()>;

    // Bind the show/hide toggle hotkey. Returns false if the OS refused to
    // register (e.g. macOS Sequoia rejects Option-only / Option+Shift combos,
    // Windows returns ERROR_HOTKEY_ALREADY_REGISTERED).
    virtual bool registerToggle(Callback cb) = 0;

    // Bind the panic hotkey: instant "stop everything, release all keys".
    // Critical for unattended concert mode in case something gets stuck.
    virtual bool registerPanic(Callback cb) = 0;

    // Windows only: SDL routes the raw Win32 message here via SDL_SYSWMEVENT.
    // Mac implementation ignores the argument because RegisterEventHotKey
    // dispatches through Carbon's event handler chain, not SDL.
    virtual void handleSDLSysWMEvent(const SDL_Event& event) = 0;

    // Construction takes the SDL window so the Windows backend can attach to
    // its HWND. Mac doesn't need it.
    static std::unique_ptr<IHotkeyService> create(SDL_Window* window);
};
