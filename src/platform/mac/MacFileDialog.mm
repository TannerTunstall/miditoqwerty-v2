#ifdef __APPLE__

#include "platform/file_dialog.h"

#include <AppKit/AppKit.h>

namespace platform {

std::string openMidiFileDialog(SDL_Window* /*parent*/) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.message = @"Select a MIDI file";
        // .allowedFileTypes is deprecated in 12+ but still works and avoids the
        // UniformTypeIdentifiers framework dep. Swap to allowedContentTypes if
        // that deprecation ever ships as a hard removal.
        panel.allowedFileTypes = @[@"mid", @"midi"];

        if ([panel runModal] != NSModalResponseOK) return {};
        NSURL* url = panel.URLs.firstObject;
        if (!url) return {};
        const char* utf8 = url.path.UTF8String;
        return utf8 ? std::string(utf8) : std::string();
    }
}

std::string openDirectoryDialog(SDL_Window* /*parent*/, const std::string& message) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.allowsMultipleSelection = NO;
        panel.canCreateDirectories = YES;
        if (!message.empty()) {
            panel.message = [NSString stringWithUTF8String:message.c_str()];
        }
        if ([panel runModal] != NSModalResponseOK) return {};
        NSURL* url = panel.URLs.firstObject;
        if (!url) return {};
        const char* utf8 = url.path.UTF8String;
        return utf8 ? std::string(utf8) : std::string();
    }
}

}  // namespace platform

#endif  // __APPLE__
