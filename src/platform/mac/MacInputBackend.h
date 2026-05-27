#pragma once
#ifdef __APPLE__

#include "platform/IInputBackend.h"

// Stage 1.3 will implement this in MacInputBackend.mm using
// CGEventCreateKeyboardEvent + CGEventPost. For Stage 1.2 the class exists so
// the build links on macOS; calls log a warning and no events are posted.
class MacInputBackend : public IInputBackend {
public:
    MacInputBackend();

    void sendKeyDown(char c) override;
    void sendKeyUp(char c, char location = 'm') override;
    void sendOutOfRangeKey(char c) override;
    void setVelocity(char c) override;

    std::vector<std::string> availableModes() const override;
    std::vector<std::string> modeTooltips() const override;
    void setMode(int modeIndex) override;
    int currentMode() const override;

    std::vector<AppTarget> availableTargets() override;
    void setTargetIndex(int index) override;
    int  currentTargetIndex() const override { return targetIndex; }

    void releaseAll() override;

private:
    // 0 = OS-aware (UCKeyTranslate reverse map for current keyboard layout)
    // 1 = Native (kVK_ANSI_* hardcoded table, layout-bypass — Roblox default)
    int mode = 1;

    // Rebuilds layoutMap_ from the current TIS keyboard layout. Called once
    // at construction and on demand when the user toggles into OS-aware mode.
    void rebuildLayoutMap();

    // 0 = first entry which is always "(active foreground)" -> pid 0
    int targetIndex = 0;
    // pid_t (==int32) for the selected target. 0 means "post to HID tap"
    // (system-wide, lands wherever focus is).
    int targetPid = 0;

    // Cached app list — refreshed when availableTargets() is called.
    std::vector<AppTarget> cachedTargets;
};

#endif // __APPLE__
