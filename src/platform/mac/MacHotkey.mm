#ifdef __APPLE__

#include "platform/IHotkeyService.h"

#include <cstdio>
#include <Carbon/Carbon.h>
#include <SDL.h>

namespace {

constexpr UInt32 kToggleSignature = 'mtq1';  // four-char OSType
constexpr UInt32 kToggleId        = 1;

class MacHotkeyService : public IHotkeyService {
public:
    MacHotkeyService() = default;

    ~MacHotkeyService() override {
        if (hotkeyRef) {
            UnregisterEventHotKey(hotkeyRef);
            hotkeyRef = nullptr;
        }
        if (handlerRef) {
            RemoveEventHandler(handlerRef);
            handlerRef = nullptr;
        }
    }

    bool registerToggle(Callback cb) override {
        toggleCb = std::move(cb);

        if (!handlerRef) {
            EventTypeSpec spec = { kEventClassKeyboard, kEventHotKeyPressed };
            OSStatus s = InstallApplicationEventHandler(
                &MacHotkeyService::handlerThunk, 1, &spec, this, &handlerRef);
            if (s != noErr) {
                std::fprintf(stderr,
                    "[MacHotkey] InstallApplicationEventHandler failed (%d)\n", (int)s);
                return false;
            }
        }

        EventHotKeyID id = { kToggleSignature, kToggleId };
        // Cmd+Shift+H. Avoids macOS Sequoia's anti-keylogger refusal of
        // Option-only / Option+Shift combos (see docs/mac-research.md).
        OSStatus s = RegisterEventHotKey(
            kVK_ANSI_H,
            cmdKey | shiftKey,
            id,
            GetApplicationEventTarget(),
            0,
            &hotkeyRef);
        if (s != noErr) {
            std::fprintf(stderr, "[MacHotkey] RegisterEventHotKey failed (%d)\n", (int)s);
            hotkeyRef = nullptr;
            return false;
        }
        std::fprintf(stderr, "[MacHotkey] Toggle hotkey registered: Cmd+Shift+H\n");
        return true;
    }

    void handleSDLSysWMEvent(const SDL_Event&) override {
        // Mac dispatches via Carbon's event chain, not SDL's syswm pipeline.
    }

private:
    static OSStatus handlerThunk(EventHandlerCallRef /*next*/, EventRef event, void* userData) {
        EventHotKeyID id;
        GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                          nullptr, sizeof(id), nullptr, &id);
        auto* self = static_cast<MacHotkeyService*>(userData);
        if (id.signature == kToggleSignature && id.id == kToggleId && self->toggleCb) {
            self->toggleCb();
        }
        return noErr;
    }

    Callback toggleCb;
    EventHotKeyRef    hotkeyRef = nullptr;
    EventHandlerRef   handlerRef = nullptr;
};

}  // namespace

std::unique_ptr<IHotkeyService> IHotkeyService::create(SDL_Window*) {
    return std::make_unique<MacHotkeyService>();
}

#endif  // __APPLE__
