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

    void releaseAll() override;

private:
    // 0 = Off (OS-aware), 1 = Emulator (layout-bypass, kVK_ANSI_*)
    int mode = 1;
};

#endif // __APPLE__
