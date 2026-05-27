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

    // Pick a specific running app as the keystroke destination, instead of
    // whatever happens to be foregrounded. On macOS this uses
    // CGEventPostToPid, which delivers events directly to the target process
    // and bypasses the system-level dead-key / IME layer (so Option+digit
    // chords arrive at Roblox as actual Option+digit instead of being
    // turned into ¡™£¢∞§¶ etc.). On Windows this is much harder to do
    // correctly with games (DirectInput ignores PostMessage), so the Windows
    // backend currently only offers the foreground target.
    struct AppTarget {
        std::string name;        // human-readable, displayed in the combo
        int         identifier;  // pid_t on macOS; 0 = active foreground
    };
    virtual std::vector<AppTarget> availableTargets() { return {{"(active foreground)", 0}}; }
    virtual void setTargetIndex(int /*index*/) {}
    virtual int  currentTargetIndex() const { return 0; }

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
    //   macOS:   ["Native (kVK)"]
    virtual std::vector<std::string> availableModes() const = 0;
    // Parallel to availableModes(); empty string for "no tooltip". The UI
    // surfaces these as hover-tooltips on each mode in the dropdown.
    virtual std::vector<std::string> modeTooltips() const { return {}; }
    virtual void setMode(int modeIndex) = 0;
    virtual int currentMode() const = 0;

    // Release any keys this backend believes are currently held. Called on
    // shutdown, on transport state transitions (Stage 2 player), and from the
    // panic hotkey (Stage 3 concert mode).
    virtual void releaseAll() = 0;
};

// Constructed once at startup; returns the platform's concrete backend.
std::unique_ptr<IInputBackend> createInputBackend();
