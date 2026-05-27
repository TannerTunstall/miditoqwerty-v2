# MIDI to Qwerty (v2)

Bridge a hardware MIDI keyboard or a `.mid` file to keystrokes for
[Virtual Piano](https://virtualpiano.net/) on Roblox (or any keyboard-input
target). Originally a Windows-only tool by
[Arijan Jakshik](https://github.com/ArijanJ); this fork adds full macOS
support, a built-in MIDI file player, a library + playlists, and unattended
concert mode.

[![Build and Release](https://github.com/TannerTunstall/miditoqwerty-v2/actions/workflows/release.yml/badge.svg?branch=master)](https://github.com/TannerTunstall/miditoqwerty-v2/actions/workflows/release.yml)

## Download

Grab the latest build from the
[Releases page](https://github.com/TannerTunstall/miditoqwerty-v2/releases/latest):

- **macOS (Apple Silicon)** — `miditoqwerty-macos-arm64.zip` (contains a
  self-contained `miditoqwerty.app`)
- **Windows (x64)** — `miditoqwerty-windows-x64.zip` (`.exe` plus `SDL2.dll`
  and `portmidi.dll`)

CI publishes a rolling **`latest`** build on every push to master, and
stable versioned releases on `v*` tags. Both platforms are built and
packaged automatically.

## Features

### Live MIDI input
- Hardware MIDI keyboard → Virtual Piano keystroke output
- 88-key range (low / high notes via Ctrl + letter chord)
- Sustain pedal (held Space)
- Velocity-aware (Alt + digit chord, 32 buckets)
- QWERTY-emulation modes for non-US layouts
  - **Windows:** Off (OS-aware) / Set 1 / Set 2 / QWERTZ
  - **macOS:** Native (kVK), physical-position keys (layout-bypass)

### Built-in MIDI file player
- Native file picker, or paste a path, or pass on the command line
- Tempo-aware scheduler (honours mid-song tempo changes via
  [craigsapp/midifile](https://github.com/craigsapp/midifile))
- Live transport: play / pause / stop / scrub / loop
- Per-channel filter (drumkit muted by default)
- CC 7 (channel volume) and CC 11 (expression) scale velocity automatically
- Per-player tempo and velocity multipliers

### Library and playlists
- Recursive directory scan with search
- Single-click load, double-click load-and-play
- Named playlists with per-track tempo / velocity overrides
- Reorderable, persisted to disk

### Concert mode (autoplay)
- Play a playlist top-to-bottom unattended
- Configurable gap between songs
- Loop modes: Off / Repeat all / Repeat one / Shuffle (no immediate repeats)
- Broken tracks are skipped rather than stalling the queue

### Global hotkeys (fire from anywhere, even inside fullscreen Roblox)
- **`Cmd+Shift+H`** (macOS) / **`Ctrl+Alt+H`** (Windows) — show / hide the
  overlay window
- **`Cmd+Shift+P`** / **`Ctrl+Alt+P`** — panic stop: halt playback and
  release every held key

### macOS: per-app keystroke targeting
- Pick a specific running app (Roblox, a Chrome tab host, etc.) and
  keystrokes go directly there via `CGEventPostToPid`, bypassing the
  system dead-key / IME layer. Required for velocity chords (Option+digit)
  to reach Roblox unmangled.

## Quick start

1. Download and unzip the release for your platform.
2. **macOS first launch:** the OS will prompt for Accessibility permission
   the first time you run it. Open *System Settings → Privacy & Security
   → Accessibility*, enable `miditoqwerty`, and re-launch.
3. Plug in a MIDI keyboard, or set up a virtual input
   (macOS: enable **IAC Driver** in *Audio MIDI Setup → Window → Show MIDI
   Studio*).
4. **macOS only:** in the Settings panel set **Target App** to *Roblox*.
5. Foreground Roblox Virtual Piano and play, or open the **Player** panel,
   browse the **Library** tab, double-click a song.

CLI auto-load works too:

```sh
./miditoqwerty path/to/song.mid    # loads on startup, doesn't auto-play
```

## Build from source

### Prerequisites (both platforms)

- CMake 3.20+
- A C++17 compiler (MSVC 2022 on Windows, AppleClang on macOS)
- Git (the project uses submodules)

### Clone with submodules

```sh
git clone --recurse-submodules https://github.com/TannerTunstall/miditoqwerty-v2.git
cd miditoqwerty-v2
# or, if you already cloned without submodules:
git submodule update --init --recursive
```

### macOS

```sh
brew install sdl2 portmidi cmake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Run from the terminal:

```sh
./build/miditoqwerty
```

Package as a `.app` bundle with bundled dylibs:

```sh
scripts/package_macos.sh build/miditoqwerty miditoqwerty.app
```

The script copies SDL2 + PortMidi out of Homebrew into the bundle's
`Contents/Frameworks/` and rewrites their install names so the `.app` runs
on a machine without Homebrew.

### Windows

1. Install **MSVC 2022** with the C++ workload.
2. Download SDL2 dev libs from libsdl.org
   (e.g. `SDL2-devel-2.32.10-VC.zip`) and extract somewhere.
3. Build PortMidi from the submodule:
   ```powershell
   cd portmidi
   cmake -B build -A x64 -DBUILD_SHARED_LIBS=ON
   cmake --build build --config Release
   cd ..
   ```
4. Configure and build:
   ```powershell
   cmake -B build -A x64 `
     -DSDL2_DIR="path\to\SDL2-2.32.10" `
     -DPORTMIDI_LIBRARY="$PWD\portmidi\build\Release\portmidi.lib" `
     -DPORTMIDI_INCLUDE_DIR="$PWD\portmidi\pm_common" `
     -DPORTTIME_INCLUDE_DIR="$PWD\portmidi\porttime"
   cmake --build build --config Release
   ```
5. To run, you need `SDL2.dll` (from the SDL2 release) and `portmidi.dll`
   (from `portmidi\build\Release`) alongside `build\Release\miditoqwerty.exe`.

## Project layout

```
src/
  main.cpp        Entry point, ImGui main loop, glue
  core/
    NoteRouter    Pure MIDI → Virtual Piano character mapping
  midi/
    MidiFilePlayer  SMF parser + tempo-aware scheduler
    MidiLibrary     Directory scan + search
    Playlist        Named playlist + on-disk persistence
    Concert         Autoplay controller (gap / loop / shuffle)
  platform/
    IInputBackend   Abstract keystroke injection
    IHotkeyService  Abstract global hotkeys (toggle + panic)
    overlay.h       Fullscreen-overlay window plumbing
    file_dialog.h   Native file / directory pickers
    win/            Windows implementations (SendInput, RegisterHotKey, …)
    mac/            macOS Obj-C++ implementations (CGEventPost, Carbon, …)
  Midi.cpp / Piano.cpp / Log.cpp / settings.cpp / themes.cpp / util.cpp
                  Carried over from the original project; lightly modernised.
vendor/gl3w/      Pre-generated gl3w sources (avoids Khronos URL flakiness)
midifile/         craigsapp/midifile submodule (SMF parser)
imgui/            Dear ImGui submodule
gl3w/             gl3w submodule (legacy generation path)
portmidi/         PortMidi submodule (built separately on Windows)
scripts/          Build / packaging helpers (package_macos.sh)
.github/workflows CI: build + package + publish to GitHub Releases
```

## Troubleshooting

- **macOS: nothing types into Roblox.** Confirm Accessibility permission is
  granted in *System Settings → Privacy & Security → Accessibility*. Confirm
  *Settings → Target App* is set to Roblox. Confirm *QWERTY Emulator* is
  *Native (kVK)*.
- **MIDI plays in the wrong key.** First, confirm the source file is in the
  expected key (run with `--load=path/to/file.mid` and listen). Some
  AI-generated MIDIs are silently transposed. If the file is correct but
  output is wrong, the most likely culprit on macOS is *Target App* being
  *(active foreground)* — the system dead-key layer eats sharp characters
  before Roblox sees them.
- **Velocity chords glitch.** Same fix — set *Target App* to Roblox so
  Option+digit bypasses macOS dead-key composition.
- **Hotkey doesn't fire on macOS Sequoia.** macOS 15 blocks
  Option-only / Option+Shift combos for synthetic global hotkeys. Default
  bindings use Cmd+Shift which is safe.
- **Stuck notes after Stop.** Hit the panic hotkey
  (`Cmd+Shift+P` / `Ctrl+Alt+P`) — releases all held keys including sustain.

## Credits

This fork builds directly on
[ArijanJ/miditoqwerty](https://github.com/ArijanJ/miditoqwerty) — the v1
foundation, the Virtual Piano character maps, the ImGui scaffolding, and
the original Windows input pipeline are all his work. Without it, none of
this would exist.

Libraries used:
- [Dear ImGui](https://github.com/ocornut/imgui) — UI
- [PortMidi](https://github.com/PortMidi/portmidi) — MIDI I/O
- [SDL2](https://www.libsdl.org/) — windowing, GL context, input event pump
- [gl3w](https://github.com/skaslev/gl3w) — OpenGL loader
- [craigsapp/midifile](https://github.com/craigsapp/midifile) — SMF parser
  (new in v2)

Themes inspired by [Monkeytype](https://github.com/monkeytypegame/monkeytype).

The v2 work — cross-platform refactor, macOS Core Graphics input backend,
built-in MIDI file player, library / playlists / concert mode, global
hotkeys, per-app targeting, and the CI/CD pipeline — was developed by
[@TannerTunstall](https://github.com/TannerTunstall) with
[Claude Code](https://claude.com/claude-code) by Anthropic as a pair.

## License

[MIT](LICENSE.md) — original license from Arijan Jakshik preserved.
All v2 contributions inherit the same terms.
