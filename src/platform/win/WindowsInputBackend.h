#pragma once
#ifdef _WIN32

#include "platform/IInputBackend.h"

class WindowsInputBackend : public IInputBackend {
public:
    WindowsInputBackend();

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
    // 0 = Off (OS-aware), 1 = Set 1, 2 = Set 2, 3 = QWERTZ
    int mode = 1;
};

#endif // _WIN32
