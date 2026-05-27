#ifdef __APPLE__

#include "MacInputBackend.h"

#include <cstdio>
#include <AppKit/AppKit.h>     // NSWorkspace / NSRunningApplication
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

// OS-aware reverse layout map. Indexed by ASCII char (0-127). Populated from
// UCKeyTranslate by iterating every CGKeyCode and recording which physical
// key + modifier produces each printable character on the *current* keyboard
// layout. Used when MacInputBackend::mode == 0 ("OS-aware").
KeyInfo gLayoutMap[128] = {};

KeyInfo lookupKeyNative(char c) {
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

// OS-aware lookup: consult the layout map populated from UCKeyTranslate.
// Falls back to the Native table for chars the layout doesn't produce (rare
// for ASCII on most Latin layouts, but possible for unusual layouts).
KeyInfo lookupKeyOSAware(char c) {
    if ((unsigned char)c < 128) {
        KeyInfo k = gLayoutMap[(unsigned char)c];
        if (k.valid) return k;
    }
    return lookupKeyNative(c);
}

KeyInfo lookupKey(char c, int mode) {
    return mode == 0 ? lookupKeyOSAware(c) : lookupKeyNative(c);
}

void postKey(CGKeyCode code, bool keyDown, CGEventFlags flags, int targetPid) {
    CGEventRef ev = CGEventCreateKeyboardEvent(NULL, code, keyDown);
    if (!ev) return;
    if (flags) CGEventSetFlags(ev, flags);
    if (targetPid > 0) {
        // Delivers directly to the target process and bypasses the system
        // event flow — Roblox sees the raw chord (e.g. Option+5) instead of
        // the Unicode composition macOS would normally substitute.
        CGEventPostToPid((pid_t)targetPid, ev);
    } else {
        // Active-foreground path: same as before, lands wherever focus is.
        CGEventPost(kCGHIDEventTap, ev);
    }
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
    rebuildLayoutMap();
}

void MacInputBackend::rebuildLayoutMap() {
    // Wipe the previous map.
    for (auto& k : gLayoutMap) k = kBad;

    TISInputSourceRef src = TISCopyCurrentKeyboardLayoutInputSource();
    if (!src) {
        std::fprintf(stderr, "[MacInputBackend] No keyboard layout source — OS-aware mode will fall back to Native.\n");
        return;
    }
    CFDataRef layoutData = (CFDataRef)TISGetInputSourceProperty(
        src, kTISPropertyUnicodeKeyLayoutData);
    if (!layoutData) {
        CFRelease(src);
        std::fprintf(stderr, "[MacInputBackend] Layout has no Unicode data — OS-aware mode will fall back to Native.\n");
        return;
    }
    const UCKeyboardLayout* layout =
        (const UCKeyboardLayout*)CFDataGetBytePtr(layoutData);
    UInt32 kbdType = LMGetKbdType();

    int populated = 0;
    for (CGKeyCode keycode = 0; keycode < 128; ++keycode) {
        for (int shift = 0; shift < 2; ++shift) {
            UInt32 modifierKeyState = shift ? ((shiftKey >> 8) & 0xFF) : 0;
            UInt32 deadKeyState = 0;
            UniCharCount actualLength = 0;
            UniChar chars[8] = {0};
            OSStatus s = UCKeyTranslate(
                layout, keycode, kUCKeyActionDisplay, modifierKeyState,
                kbdType, kUCKeyTranslateNoDeadKeysMask,
                &deadKeyState, 8, &actualLength, chars);
            if (s != noErr || actualLength == 0) continue;
            UniChar uc = chars[0];
            if (uc > 127) continue;             // ASCII only
            if (uc < 32 || uc == 127) continue; // skip control chars
            unsigned char idx = (unsigned char)uc;
            // First match wins. With shift=0 iterated first, unshifted chars
            // get priority — letters land as {their key, false}, then their
            // shifted uppercase lands as {same key, true} on the next pass.
            if (!gLayoutMap[idx].valid) {
                gLayoutMap[idx] = {keycode, shift != 0, true};
                ++populated;
            }
        }
    }
    CFRelease(src);

    // Space is special — sustain pedal always uses kVK_Space regardless of
    // layout. UCKeyTranslate gives it as Space on every sane layout, but be
    // defensive.
    if (!gLayoutMap[' '].valid) {
        gLayoutMap[' '] = {(CGKeyCode)kVK_Space, false, true};
    }

    std::fprintf(stderr,
        "[MacInputBackend] OS-aware layout map: %d printable ASCII chars resolved.\n",
        populated);
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
    KeyInfo k = lookupKey(c, mode);
    if (!k.valid) return;
    if (k.shift) {
        postKey((CGKeyCode)kVK_Shift, true, 0, targetPid);
        postKey(k.code, true, kCGEventFlagMaskShift, targetPid);
        postKey((CGKeyCode)kVK_Shift, false, 0, targetPid);
    } else {
        postKey(k.code, true, 0, targetPid);
    }
}

void MacInputBackend::sendKeyUp(char c, char /*location*/) {
    // Mac CGKeyCode is physical-position; same code releases whether the press
    // was shifted or not, so we don't need findIndex-style char translation.
    KeyInfo k = lookupKey(c, mode);
    if (!k.valid) return;
    postKey(k.code, false, 0, targetPid);
}

void MacInputBackend::sendOutOfRangeKey(char c) {
    KeyInfo k = lookupKey(c, mode);
    if (!k.valid) return;
    postKey((CGKeyCode)kVK_Control, true, 0, targetPid);
    if (k.shift) postKey((CGKeyCode)kVK_Shift, true, 0, targetPid);
    CGEventFlags f = kCGEventFlagMaskControl |
                     (k.shift ? kCGEventFlagMaskShift : (CGEventFlags)0);
    postKey(k.code, true,  f, targetPid);
    postKey(k.code, false, f, targetPid);
    if (k.shift) postKey((CGKeyCode)kVK_Shift, false, 0, targetPid);
    postKey((CGKeyCode)kVK_Control, false, 0, targetPid);
}

void MacInputBackend::setVelocity(char c) {
    KeyInfo k = lookupKey(c, mode);
    if (!k.valid) return;
    postKey((CGKeyCode)kVK_Option, true, 0, targetPid);
    if (k.shift) postKey((CGKeyCode)kVK_Shift, true, 0, targetPid);
    CGEventFlags f = kCGEventFlagMaskAlternate |
                     (k.shift ? kCGEventFlagMaskShift : (CGEventFlags)0);
    postKey(k.code, true,  f, targetPid);
    postKey(k.code, false, f, targetPid);
    if (k.shift) postKey((CGKeyCode)kVK_Shift, false, 0, targetPid);
    postKey((CGKeyCode)kVK_Option, false, 0, targetPid);
}

std::vector<std::string> MacInputBackend::availableModes() const {
    // Two modes, matching the spirit of the Windows backend:
    //   0 OS-aware — UCKeyTranslate reverse lookup against the current
    //     keyboard layout (so '!' goes through whichever physical key + mod
    //     produces '!' on the user's layout). Use this when the receiving
    //     app honours the macOS keyboard layout (TextEdit, browsers, etc.).
    //   1 Native — hardcoded kVK_ANSI_* table, layout-bypass. Use this for
    //     Roblox and anything else that reads physical keys directly.
    // macOS doesn't have the Set 1 / Set 2 / QWERTZ distinction that
    // Windows exposes — CGKeyCode is already physical-position.
    return { "OS-aware (layout)", "Native (kVK)" };
}

std::vector<std::string> MacInputBackend::modeTooltips() const {
    return {
        "Uses UCKeyTranslate against the current macOS keyboard layout.\n"
        "Equivalent to Windows 'Off (OS-aware)' — works in TextEdit,\n"
        "browsers, and other apps that respect the layout.",
        "Physical-position keys (kVK_ANSI_*), layout-bypass.\n"
        "Equivalent to Windows 'Set 1' — what Roblox expects."
    };
}

void MacInputBackend::setMode(int modeIndex) {
    if (modeIndex < 0 || modeIndex > 1) modeIndex = 1;  // clamp; default Native
    mode = modeIndex;
    if (mode == 0) {
        // Refresh the layout map in case the user changed their input source
        // after construction.
        rebuildLayoutMap();
    }
}

int MacInputBackend::currentMode() const { return mode; }

void MacInputBackend::releaseAll() {
    // Best-effort: release sustain if it might be held. Per-note state lives
    // in NoteRouter, which calls sendKeyUp for each held note separately.
    postKey((CGKeyCode)kVK_Space, false, (CGEventFlags)0, targetPid);
}

std::vector<IInputBackend::AppTarget> MacInputBackend::availableTargets() {
    std::vector<AppTarget> result;
    result.push_back({"(active foreground)", 0});

    @autoreleasepool {
        NSArray<NSRunningApplication*>* apps =
            [[NSWorkspace sharedWorkspace] runningApplications];
        for (NSRunningApplication* app in apps) {
            // Skip background daemons, agents, the Dock, etc. Regular apps
            // are the ones a user would expect to target.
            if (app.activationPolicy != NSApplicationActivationPolicyRegular) continue;
            NSString* name = app.localizedName;
            if (!name) continue;
            const char* utf8 = name.UTF8String;
            if (!utf8 || !*utf8) continue;
            result.push_back({std::string(utf8), (int)app.processIdentifier});
        }
    }

    cachedTargets = result;
    // Best-effort: if the previously selected pid no longer exists, fall
    // back to "(active foreground)".
    if (targetPid > 0) {
        bool stillThere = false;
        for (size_t i = 0; i < cachedTargets.size(); ++i) {
            if (cachedTargets[i].identifier == targetPid) {
                targetIndex = (int)i;
                stillThere = true;
                break;
            }
        }
        if (!stillThere) {
            targetIndex = 0;
            targetPid = 0;
        }
    }
    return result;
}

void MacInputBackend::setTargetIndex(int index) {
    if (cachedTargets.empty()) availableTargets();
    if (index < 0 || index >= (int)cachedTargets.size()) return;
    targetIndex = index;
    targetPid = cachedTargets[index].identifier;
    std::fprintf(stderr, "[MacInputBackend] target -> %s (pid=%d)\n",
                 cachedTargets[index].name.c_str(), targetPid);
}

#endif  // __APPLE__
