#pragma once
#include <memory>
#include <string>
#include <vector>

// Abstract keyboard-injection backend. Implementations live under platform/win
// and platform/mac. The note router (core/NoteRouter) talks only through this
// interface so the same routing logic drives Windows SendInput and macOS
// CGEventPost without #ifdefs.
class IInputBackend {
public:
    virtual ~IInputBackend() = default;

    // Note-output operations. `c` is a Virtual Piano character produced by the
    // note router; `location` is one of 'l' (low octave, out-of-range low),
    // 'm' (middle, the 61-key span), or 'h' (high octave, out-of-range high).
    virtual void sendKeyDown(char c) = 0;
    virtual void sendKeyUp(char c, char location = 'm') = 0;

    // Out-of-range octave: send Ctrl+letter so Virtual Piano shifts octave.
    virtual void sendOutOfRangeKey(char c) = 0;

    // Velocity selector: Alt+character chord that Virtual Piano interprets as
    // a velocity bucket (see velocities/velocityList in Qwerty.h).
    virtual void setVelocity(char c) = 0;

    // Mode selection. Available modes are platform-specific:
    //   Windows: ["Off (OS-aware)", "Set 1", "Set 2", "QWERTZ"]
    //   macOS:   ["Off (OS-aware)", "Emulator"]
    virtual std::vector<std::string> availableModes() const = 0;
    virtual void setMode(int modeIndex) = 0;
    virtual int currentMode() const = 0;

    // Release any keys this backend believes are currently held. Called on
    // shutdown, on transport state transitions (Stage 2 player), and from the
    // panic hotkey (Stage 3 concert mode).
    virtual void releaseAll() = 0;
};

// Constructed once at startup; returns the platform's concrete backend.
std::unique_ptr<IInputBackend> createInputBackend();
