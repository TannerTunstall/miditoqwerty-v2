#ifdef __APPLE__

#include "MacInputBackend.h"

#include <cstdio>
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <Foundation/Foundation.h>

namespace {

// Map Virtual Piano character -> physical Mac keycode + whether Shift is part
// of the chord. We use the kVK_ANSI_* constants from Carbon's Events.h: those
// are physical-position identifiers (kVK_ANSI_A is always the key in the
// US-QWERTY "A" spot, regardless of the active keyboard layout). That gives
// us the same layout-bypass semantic Roblox needs on Windows from Set 1/2.
struct KeyInfo { CGKeyCode code; bool shift; bool valid; };
static constexpr KeyInfo kBad = {0, false, false};

KeyInfo lookupKey(char c) {
    switch (c) {
        // Digits
        case '0': return {kVK_ANSI_0, false, true};
        case '1': return {kVK_ANSI_1, false, true};
        case '2': return {kVK_ANSI_2, false, true};
        case '3': return {kVK_ANSI_3, false, true};
        case '4': return {kVK_ANSI_4, false, true};
        case '5': return {kVK_ANSI_5, false, true};
        case '6': return {kVK_ANSI_6, false, true};
        case '7': return {kVK_ANSI_7, false, true};
        case '8': return {kVK_ANSI_8, false, true};
        case '9': return {kVK_ANSI_9, false, true};

        // Shifted digits (Virtual Piano upper-row notes)
        case ')': return {kVK_ANSI_0, true, true};
        case '!': return {kVK_ANSI_1, true, true};
        case '@': return {kVK_ANSI_2, true, true};
        case '#': return {kVK_ANSI_3, true, true};
        case '$': return {kVK_ANSI_4, true, true};
        case '%': return {kVK_ANSI_5, true, true};
        case '^': return {kVK_ANSI_6, true, true};
        case '&': return {kVK_ANSI_7, true, true};
        case '*': return {kVK_ANSI_8, true, true};
        case '(': return {kVK_ANSI_9, true, true};

        // Lowercase letters
        case 'a': return {kVK_ANSI_A, false, true};
        case 'b': return {kVK_ANSI_B, false, true};
        case 'c': return {kVK_ANSI_C, false, true};
        case 'd': return {kVK_ANSI_D, false, true};
        case 'e': return {kVK_ANSI_E, false, true};
        case 'f': return {kVK_ANSI_F, false, true};
        case 'g': return {kVK_ANSI_G, false, true};
        case 'h': return {kVK_ANSI_H, false, true};
        case 'i': return {kVK_ANSI_I, false, true};
        case 'j': return {kVK_ANSI_J, false, true};
        case 'k': return {kVK_ANSI_K, false, true};
        case 'l': return {kVK_ANSI_L, false, true};
        case 'm': return {kVK_ANSI_M, false, true};
        case 'n': return {kVK_ANSI_N, false, true};
        case 'o': return {kVK_ANSI_O, false, true};
        case 'p': return {kVK_ANSI_P, false, true};
        case 'q': return {kVK_ANSI_Q, false, true};
        case 'r': return {kVK_ANSI_R, false, true};
        case 's': return {kVK_ANSI_S, false, true};
        case 't': return {kVK_ANSI_T, false, true};
        case 'u': return {kVK_ANSI_U, false, true};
        case 'v': return {kVK_ANSI_V, false, true};
        case 'w': return {kVK_ANSI_W, false, true};
        case 'x': return {kVK_ANSI_X, false, true};
        case 'y': return {kVK_ANSI_Y, false, true};
        case 'z': return {kVK_ANSI_Z, false, true};

        // Uppercase = Shift + letter
        case 'A': return {kVK_ANSI_A, true, true};
        case 'B': return {kVK_ANSI_B, true, true};
        case 'C': return {kVK_ANSI_C, true, true};
        case 'D': return {kVK_ANSI_D, true, true};
        case 'E': return {kVK_ANSI_E, true, true};
        case 'F': return {kVK_ANSI_F, true, true};
        case 'G': return {kVK_ANSI_G, true, true};
        case 'H': return {kVK_ANSI_H, true, true};
        case 'I': return {kVK_ANSI_I, true, true};
        case 'J': return {kVK_ANSI_J, true, true};
        case 'K': return {kVK_ANSI_K, true, true};
        case 'L': return {kVK_ANSI_L, true, true};
        case 'M': return {kVK_ANSI_M, true, true};
        case 'N': return {kVK_ANSI_N, true, true};
        case 'O': return {kVK_ANSI_O, true, true};
        case 'P': return {kVK_ANSI_P, true, true};
        case 'Q': return {kVK_ANSI_Q, true, true};
        case 'R': return {kVK_ANSI_R, true, true};
        case 'S': return {kVK_ANSI_S, true, true};
        case 'T': return {kVK_ANSI_T, true, true};
        case 'U': return {kVK_ANSI_U, true, true};
        case 'V': return {kVK_ANSI_V, true, true};
        case 'W': return {kVK_ANSI_W, true, true};
        case 'X': return {kVK_ANSI_X, true, true};
        case 'Y': return {kVK_ANSI_Y, true, true};
        case 'Z': return {kVK_ANSI_Z, true, true};

        // Sustain pedal
        case ' ': return {(CGKeyCode)kVK_Space, false, true};

        default: return kBad;
    }
}

void postKey(CGKeyCode code, bool keyDown, CGEventFlags flags) {
    CGEventRef ev = CGEventCreateKeyboardEvent(NULL, code, keyDown);
    if (!ev) return;
    if (flags) CGEventSetFlags(ev, flags);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

bool ensureAccessibility() {
    // Surface the system prompt the first time we're run unsigned. The user
    // grants in System Settings -> Privacy & Security -> Accessibility, then
    // must re-launch the binary for it to take effect.
    NSDictionary* opts = @{ (__bridge id)kAXTrustedCheckOptionPrompt: @YES };
    bool trusted = AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts);
    return trusted;
}

}  // namespace

MacInputBackend::MacInputBackend() {
    bool trusted = ensureAccessibility();
    if (trusted) {
        std::fprintf(stderr, "[MacInputBackend] Accessibility granted; key injection live.\n");
    } else {
        std::fprintf(stderr,
            "[MacInputBackend] Accessibility NOT granted.\n"
            "                  Open System Settings -> Privacy & Security -> Accessibility,\n"
            "                  enable this binary, then restart the app.\n"
            "                  Until then, sendKey* calls will be silently dropped by macOS.\n");
    }
}

// Why explicit modifier press/release instead of CGEventSetFlags:
//
// Roblox on macOS does NOT honour synthetic modifier flags set via
// kCGEventFlagMaskShift / kCGEventFlagMaskControl / kCGEventFlagMaskAlternate
// on a CGEvent. It only observes the actual keyboard-modifier state derived
// from real Shift / Control / Option key events. If we just slap a flag on
// the event, Roblox sees the bare character — e.g. our '!' arrives as '1',
// which on Virtual Piano means the sharp gets stripped. C♯ minor played
// that way sounds like C major.
//
// So we mirror the Windows backend pattern: send the modifier key down,
// send the character key, send the modifier key up. For chord operations
// (Ctrl + letter, Alt + char) the character is then released too; for the
// per-note sendKeyDown the character is left held, with sendKeyUp dropping
// it later, exactly like the Windows code in inpututils.cpp does.

void MacInputBackend::sendKeyDown(char c) {
    KeyInfo k = lookupKey(c);
    if (!k.valid) return;
    if (k.shift) {
        postKey((CGKeyCode)kVK_Shift, true, 0);
        postKey(k.code, true, kCGEventFlagMaskShift);
        postKey((CGKeyCode)kVK_Shift, false, 0);
    } else {
        postKey(k.code, true, 0);
    }
}

void MacInputBackend::sendKeyUp(char c, char /*location*/) {
    // Mac CGKeyCode is physical-position; same code releases whether the press
    // was shifted or not, so we don't need findIndex-style char translation.
    KeyInfo k = lookupKey(c);
    if (!k.valid) return;
    postKey(k.code, false, 0);
}

void MacInputBackend::sendOutOfRangeKey(char c) {
    KeyInfo k = lookupKey(c);
    if (!k.valid) return;
    postKey((CGKeyCode)kVK_Control, true, 0);
    if (k.shift) postKey((CGKeyCode)kVK_Shift, true, 0);
    CGEventFlags f = kCGEventFlagMaskControl |
                     (k.shift ? kCGEventFlagMaskShift : (CGEventFlags)0);
    postKey(k.code, true,  f);
    postKey(k.code, false, f);
    if (k.shift) postKey((CGKeyCode)kVK_Shift, false, 0);
    postKey((CGKeyCode)kVK_Control, false, 0);
}

void MacInputBackend::setVelocity(char c) {
    KeyInfo k = lookupKey(c);
    if (!k.valid) return;
    postKey((CGKeyCode)kVK_Option, true, 0);
    if (k.shift) postKey((CGKeyCode)kVK_Shift, true, 0);
    CGEventFlags f = kCGEventFlagMaskAlternate |
                     (k.shift ? kCGEventFlagMaskShift : (CGEventFlags)0);
    postKey(k.code, true,  f);
    postKey(k.code, false, f);
    if (k.shift) postKey((CGKeyCode)kVK_Shift, false, 0);
    postKey((CGKeyCode)kVK_Option, false, 0);
}

std::vector<std::string> MacInputBackend::availableModes() const {
    // macOS collapses Windows' Set 1 / Set 2 / QWERTZ into a single mode
    // because CGKeyCode is already physical-position (layout-bypass). The
    // Windows-style OS-aware mode would be UCKeyTranslate-based on Mac;
    // not implemented yet because Roblox needs the physical-position path.
    return { "Native (kVK)" };
}

std::vector<std::string> MacInputBackend::modeTooltips() const {
    return {
        "Physical-position keys (kVK_ANSI_*), layout-bypass.\n"
        "Equivalent to Windows Set 1; what Roblox expects."
    };
}

void MacInputBackend::setMode(int /*modeIndex*/) {
    mode = 0;  // single mode available, ignore index
}

int MacInputBackend::currentMode() const { return mode; }

void MacInputBackend::releaseAll() {
    // Best-effort: release sustain if it might be held. Per-note state lives
    // in NoteRouter, which calls sendKeyUp for each held note separately.
    postKey((CGKeyCode)kVK_Space, false, (CGEventFlags)0);
}

#endif  // __APPLE__
