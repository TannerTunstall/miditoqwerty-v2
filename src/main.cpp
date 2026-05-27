#include <iostream>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <thread>
#include <future>

#include <SDL.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"

#include "themes.h"

#include "Log.h"
#include "Piano.h"
#include "Midi.h"

#include "util.h"
#include "settings.h"
#include "Qwerty.h"

#include "platform/IInputBackend.h"
#include "platform/IHotkeyService.h"
#include "platform/overlay.h"
#include "platform/file_dialog.h"
#include "core/NoteRouter.h"
#include "midi/MidiFilePlayer.h"
#include "midi/MidiLibrary.h"
#include "midi/Playlist.h"
#include "midi/Concert.h"

#include "GL/gl3w.h"

#define POSSIBLYEDITABLE (ImGuiWindowFlags_NoBringToFrontOnFocus | (!windowsEditable ? \
                                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | \
                                             ImGuiWindowFlags_NoCollapse \
                                             : \
                                             ImGuiWindowFlags_None))

// Settings
int logStuff;

// state
bool resetting = false;
bool rightDown = false;

// saved in settings
std::string defaultFont;
std::string defaultTheme;

// initialize externs
std::string currentFont;
std::string currentTheme;

// Defaults match ImGui's StyleColorsDark so the UI is legible even when no
// themes/default.theme file is on disk (LoadTheme silently no-ops in that
// case). Real values get overwritten when a theme loads successfully.
ImVec4 gBackgroundColor = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
ImVec4 gNoteColor       = ImVec4(0.96f, 0.16f, 0.16f, 1.00f);
ImVec4 gNoteNameColor   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);

static int showTitlebar = 1;
static int windowOpacity = 100;
static int windowsEditable = 0;

static int smallLayout = 0;

static int alwaysontop = 1;

static int qwertyEmulator = 1;

static int enableOutput = 1;
static int eightyeightkey = 1;
static int sustain = 1;
static int velocity = 1;

int sustainCutoff = 64;

SDL_Window* window;

SettingsHandler settingsHandler;

Piano piano;
Midi midi;
PmTimestamp lastNotePlayed = 0;

Log logger;

std::unique_ptr<IInputBackend> input;
std::unique_ptr<NoteRouter> noteRouter;
std::unique_ptr<MidiFilePlayer> filePlayer;
std::unique_ptr<MidiLibrary> library;
std::unique_ptr<PlaylistManager> playlists;
std::unique_ptr<Concert> concert;
std::unique_ptr<IHotkeyService> hotkeys;
bool overlayHidden = false;

void applyBackendMode() {
    if (!input) return;
    const auto modes = input->availableModes();
    if (qwertyEmulator < 0 || qwertyEmulator >= (int)modes.size()) {
        qwertyEmulator = 0;
    }
    input->setMode(qwertyEmulator);
    printf("Routing input backend mode -> %d (%s)\n",
           qwertyEmulator, modes[qwertyEmulator].c_str());
}

void refreshSettings(){
    ImGui::LoadIniSettingsFromDisk((smallLayout ? "layout_small.ini" : "layout_tall.ini"));
    SDL_SetWindowOpacity(window, (float)windowOpacity / 100);
    SDL_SetWindowBordered(window, (showTitlebar ? SDL_TRUE : SDL_FALSE));
    SDL_SetWindowAlwaysOnTop(window, (alwaysontop ? SDL_TRUE : SDL_FALSE));
    applyBackendMode();
}

void resetSettings() {
    alwaysontop = true;

    enableOutput = true;

    eightyeightkey = true;
    sustain = true;
    velocity = true;

    sustainCutoff = 64;

    showTitlebar = true;
    windowOpacity = 100;
    windowsEditable = false;

    currentTheme = "default";
    defaultTheme = "themes/default.theme";
    logStuff = true;
    LoadTheme(defaultTheme);
    refreshSettings();

    settingsHandler.DumpSettings();
}

void pollCallback(PmTimestamp timestamp, uint8_t status, PmMessage Data1, PmMessage Data2) {
    // Display always reflects the keyboard; output gating happens in NoteRouter.
    const bool isNoteOn  = (status >= 0x90 && status <= 0x9F);
    const bool isNoteOff = (status >= 0x80 && status <= 0x8F);
    // NoteOn with velocity 0 is the standard "running-status NoteOff" — every
    // SMF file uses this convention. Without treating it as a release the
    // piano-display key never clears and the whole keyboard turns red.
    if (isNoteOn && Data2 > 0)        piano.down((int)Data1, (int)Data2);
    if (isNoteOff || (isNoteOn && Data2 == 0)) piano.up((int)Data1);

    if (noteRouter) noteRouter->onMidiEvent(timestamp, status, Data1, Data2);
}

int main(int argc, char* argv[]) {
    // When launched from a .app bundle the working directory is "/", which
    // means settings.dat / log.txt / library scans all hit a read-only spot.
    // Detect bundle launch via argv[0] and chdir to the user data dir.
    if (argc > 0) {
        std::string arg0 = argv[0];
        if (arg0.find("/Contents/MacOS/") != std::string::npos) {
            std::string dataDir;
            if (const char* home = std::getenv("HOME")) {
                dataDir = std::string(home) + "/Documents/miditoqwerty";
            }
            if (!dataDir.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(dataDir, ec);
                std::filesystem::current_path(dataDir, ec);
                printf("Bundle launch: chdir to %s\n", dataDir.c_str());
            }
        }
    }

    // Optional first-positional arg: path to a .mid to auto-load on startup.
    // Supports drag-and-drop on the binary and CLI testing.
    const char* autoLoadPath = (argc > 1) ? argv[1] : nullptr;

#ifdef _WIN32
    // GUI subsystem on Windows has no usable stdout; redirect to log.txt as
    // before. On macOS/Linux we leave stdout on the terminal.
    FILE* stdoutNew;
    freopen_s(&stdoutNew, "log.txt", "w", stdout);
    setvbuf(stdout, NULL, _IONBF, 0);
#endif

    input = createInputBackend();
    noteRouter = std::make_unique<NoteRouter>(input.get(), &logger);
    noteRouter->bindSettings(&enableOutput, &eightyeightkey, &sustain, &velocity, &sustainCutoff);
    filePlayer = std::make_unique<MidiFilePlayer>(noteRouter.get(), &piano, &logger);
    library    = std::make_unique<MidiLibrary>();
    concert    = std::make_unique<Concert>(filePlayer.get(), &logger);

    // Default library root. Platform-conventional location; the user can
    // change it at runtime via the Library tab's Browse button. No persistence
    // yet — a custom root reverts to this default on next launch (will get
    // proper string-setting persistence in Stage 3b/c).
    {
        std::string defaultRoot;
        if (const char* home = std::getenv("HOME")) {
            defaultRoot = std::string(home) + "/Documents/miditoqwerty/library";
        } else if (const char* userprofile = std::getenv("USERPROFILE")) {
            defaultRoot = std::string(userprofile) + "/Documents/miditoqwerty/library";
        }
        if (!defaultRoot.empty() && std::filesystem::exists(defaultRoot)) {
            int n = library->scanDirectory(defaultRoot);
            printf("Library: scanned %s (%d entries)\n", defaultRoot.c_str(), n);
        } else {
            // Initialise the root path even if the directory doesn't exist
            // yet, so the UI shows the suggested location.
            library->scanDirectory(defaultRoot);  // no-op if dir absent
        }
    }

    // Default playlists directory: ~/Documents/miditoqwerty/playlists/
    {
        std::string defaultPlRoot;
        if (const char* home = std::getenv("HOME")) {
            defaultPlRoot = std::string(home) + "/Documents/miditoqwerty/playlists";
        } else if (const char* userprofile = std::getenv("USERPROFILE")) {
            defaultPlRoot = std::string(userprofile) + "/Documents/miditoqwerty/playlists";
        }
        playlists = std::make_unique<PlaylistManager>(defaultPlRoot);
        int n = playlists->load();
        printf("Playlists: loaded %d from %s\n", n, defaultPlRoot.c_str());
    }

    if (autoLoadPath) {
        if (filePlayer->load(autoLoadPath)) {
            printf("Auto-loaded: %s (%.2fs)\n", autoLoadPath, filePlayer->durationSeconds());
        } else {
            printf("Auto-load failed for: %s\n", autoLoadPath);
        }
    }

    settingsHandler.AddSetting("Always on top", &alwaysontop);
    settingsHandler.AddSetting("Editable windows", &windowsEditable);
    settingsHandler.AddSetting("Show titlebar", &showTitlebar);
    settingsHandler.AddSetting("Window opacity", &windowOpacity);
    settingsHandler.AddSetting("Small layout", &smallLayout);
    settingsHandler.AddSetting("Enable output", &enableOutput);
    settingsHandler.AddSetting("88-key support", &eightyeightkey);
    settingsHandler.AddSetting("Sustain", &sustain);
    settingsHandler.AddSetting("Velocity", &velocity);
    settingsHandler.AddSetting("Sustain cutoff", &sustainCutoff);
    settingsHandler.AddSetting("Log stuff", &logStuff);
    settingsHandler.AddSetting("QWERTY emulation", &qwertyEmulator);

    if (midi.deviceID < 0) {
        printf("No MIDI input device found at startup. The app will launch "
               "anyway — attach one later from the Settings > MIDI Input combo, "
               "or use the built-in file player (Stage 2).\n");
    }

    fflush(stdout);

    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }
#ifdef __APPLE__
    // macOS Core profile only supports OpenGL 3.2+, which requires GLSL 150
    // (Core profile rejects GLSL 130). Forward-compatible flag is required.
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // Original behaviour: GL 3.0 + GLSL 130, Core profile (works on Windows).
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL        |
                                                     SDL_WINDOW_ALLOW_HIGHDPI |
                                                     SDL_WINDOW_RESIZABLE     |
                                                     SDL_WINDOW_ALWAYS_ON_TOP );
    // Default size bumped from 435x550 so the Settings panel doesn't need
    // scrolling out-of-the-box. Window is resizable so users can shrink it.
    window = SDL_CreateWindow("Midi to Qwerty", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 520, 800, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Promote the window to a level that floats over fullscreen apps (Roblox).
    // No-op on Windows where SDL_WINDOW_ALWAYS_ON_TOP already does the job.
    configureWindowOverlay(window);

    // Global show/hide hotkey: Cmd+Shift+H on Mac, Ctrl+Alt+H on Windows.
    // Fires even when Roblox has fullscreen focus.
    hotkeys = IHotkeyService::create(window);
    if (hotkeys) {
        hotkeys->registerToggle([]() {
            overlayHidden = !overlayHidden;
            if (overlayHidden) {
                SDL_HideWindow(window);
            } else {
                SDL_ShowWindow(window);
                // Re-apply overlay level — some platforms reset window state
                // on hide/show, and the overlay must keep floating over Roblox.
                configureWindowOverlay(window);
            }
        });
        // Panic: stop concert + player, release all held keys. Use when
        // something gets stuck during unattended concert playback.
        hotkeys->registerPanic([]() {
            logger.AddLog("PANIC: stopping all playback\n");
            if (concert)    concert->stop();
            if (filePlayer) filePlayer->stop();
            if (noteRouter) noteRouter->releaseAll();
        });
    }

    // Initialize OpenGL loader
    bool err = gl3wInit() != 0;
    if (err) {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL; // Manual setting with LoadIniFileXXX
    ImGui::LoadIniSettingsFromDisk((smallLayout?"layout_small.ini" : "layout_tall.ini"));

    std::string initializedFont;

    fflush(stdout);

    // HiDPI: rasterize fonts at the framebuffer's true pixel density (2x on a
    // Retina display) and ask ImGui to display them at 1/scale, so the visual
    // size stays the same as on a 1x display but the glyph atlas is sharp.
    // Without this, fonts appear blurry on macOS Retina.
    int win_w_pt = 0, win_h_pt = 0, win_w_px = 0, win_h_px = 0;
    SDL_GetWindowSize(window, &win_w_pt, &win_h_pt);
    SDL_GL_GetDrawableSize(window, &win_w_px, &win_h_px);
    const float dpiScale = (win_w_pt > 0 && win_w_px > 0)
        ? (float)win_w_px / (float)win_w_pt
        : 1.0f;
    printf("Display scale: %.2fx (window %dx%d pt, drawable %dx%d px)\n",
           dpiScale, win_w_pt, win_h_pt, win_w_px, win_h_px);

    // Default font, rasterized at the scaled size.
    {
        ImFontConfig cfg;
        cfg.SizePixels = 13.0f * dpiScale;
        cfg.OversampleH = 3;
        cfg.OversampleV = 1;
        io.Fonts->AddFontDefault(&cfg);
    }

    // Load fonts. The original release ships a fonts/ directory alongside the
    // binary; tolerate it being absent (built-from-source case) so we don't
    // throw out of recursive_directory_iterator on launch.
    if (std::filesystem::exists("fonts") && std::filesystem::is_directory("fonts")) {
        for (auto& p : std::filesystem::recursive_directory_iterator("fonts"))
        {
            if (p.path().extension() == ".ttf") {

                std::string relativePath = p.path().stem().string();
                if (relativePath == defaultFont) continue;

                std::string fullPath = "fonts/" + relativePath + ".ttf";

                if (fullPath == initializedFont) continue;

                printf("Font found: %s\n", fullPath.c_str());

                io.Fonts->AddFontFromFileTTF(fullPath.c_str(), 13.0f * dpiScale);
            }
        }
    } else {
        printf("No fonts/ directory found; using ImGui default font only.\n");
    }

    // Counter-scale display so the rasterized-at-2x glyphs render at 1x visual size.
    io.FontGlobalScale = 1.0f / dpiScale;

    fflush(stdout);

    (void) io;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowTitleAlign = ImVec2(0.5f, 0.5f);
    ImGui::GetStyle().WindowRounding = 8.0f;
    ImGui::GetStyle().FrameRounding = 4.0f;
    ImGui::GetStyle().GrabRounding = 4.0f;

    // Setup Platform/Renderer bindings
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    if (!settingsHandler.LoadSettings()) {
        printf("Could not load settings - resetting them\n");
        resetSettings();
        settingsHandler.DumpSettings();
    }

    if (smallLayout) {
        SDL_SetWindowSize(window, 520, 420);
        printf("Loaded small layout\n");
    }
    else {
        SDL_SetWindowSize(window, 520, 800);
        printf("Loaded tall layout\n");
    }

    refreshSettings();

    // Our state
    bool show_midi_window = true;
    bool show_piano_window = true;
    bool show_log_window = true;
    bool rainbowMode = false;

    // Set up thread
    auto thr = [](std::future<void> futureObj) {
        while (futureObj.wait_for(std::chrono::milliseconds(1)) == std::future_status::timeout) {
            midi.poll(pollCallback, true);
        }
        printf("Finished MIDI thread\n");
    };

    std::promise<void> midiThreadExitSignal;
    std::future<void> futureObj = midiThreadExitSignal.get_future();
    std::thread midithread(thr, std::move(futureObj));

    // Main loop
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (hotkeys) hotkeys->handleSDLSysWMEvent(event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
                done = true;
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                    done = true;
            }

            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    rightDown = true;
                }
            }
            if (event.type == SDL_MOUSEBUTTONUP) {
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    rightDown = false;
                }
            }
        }

        // Drive autoplay; no-op when concert isn't running.
        if (concert) concert->tick();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();

        // Default window layout for the no-ini case (built-from-source). Reads
        // the live SDL window size so resizing the host window rearranges the
        // first-time layout proportionally. ImGui-saved positions still win on
        // subsequent runs when layout_*.ini is present.
        int sdlW = 0, sdlH = 0;
        SDL_GetWindowSize(window, &sdlW, &sdlH);
        const float layoutW = (float)sdlW;
        const float layoutH = (float)sdlH;
        const float midiH   = 82.0f;   // fits Playing: + Qwerty: lines at any DPI
        const float kbdH    = smallLayout ? 80.0f : 100.0f;
        const float midH    = layoutH - midiH - kbdH - 10.0f;
        const float leftW   = layoutW * 0.46f;
        const float rightW  = layoutW - leftW - 5.0f;
        // Right column splits into Player (top) + Log (bottom).
        const float playerH = midH * 0.42f;
        const float logH    = midH - playerH - 5.0f;

        if (show_midi_window) {
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(layoutW, midiH), ImGuiCond_FirstUseEver);
            ImGui::Begin("Midi", NULL, POSSIBLYEDITABLE);
            std::ostringstream os;
            auto current_notes = piano.current_notes();
            for (auto &note : current_notes) {
                os << midiNoteString(note) << " ";
            }
            std::string playing = os.str();
            ImGui::Text("Playing: ");
            ImGui::SameLine();
            if (!playing.empty()) {
                ImGui::TextColored(gNoteNameColor, "%s", playing.c_str());
            } else {
                ImGui::Text("");
            }
            ImGui::Text("Qwerty: ");
            ImGui::SameLine();
            if (!playing.empty()) {
                for (auto& note : current_notes) {
                    if (note > 0 && note < 36) {
                        ImGui::TextColored(ImVec4(1, 1, 0, 1), "%c ", lowNotes.c_str()[abs(note - 35)]);
                    }
                    else if (note >= 36 && note <= 96) {
                        ImGui::TextColored(ImVec4(0, 0, 1, 1), "%c ", letterNoteMap.c_str()[note - 36]);;
                    }
                    else if (note > 96 && note < 122) {
                        ImGui::TextColored(ImVec4(1, 0, 0, 1), "%c ", highNotes.c_str()[note - 97]);
                    }
                    else {
                        logger.AddLog("Could not find key %d\n", note);
                    }
                    ImGui::SameLine();
                }

            } else { ImGui::Text(""); }
            ImGui::End();
        }

        if (rainbowMode) {
            static int r = 0; static int g = 0; static int b = 0;

            ImVec4 normalizedRainbow = ImVec4((float)r / 255, (float)g / 255, (float)b / 255, 1.0f);

            advanceRainbow(&r, &b, &g);

            gBackgroundColor    = normalizedRainbow;
            gNoteColor          = normalizedRainbow;
            gNoteNameColor      = normalizedRainbow;
        }

        if (show_piano_window) {
            ImGui::SetNextWindowPos(ImVec2(0, layoutH - kbdH), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(layoutW, kbdH), ImGuiCond_FirstUseEver);
            piano.draw(&show_piano_window, windowsEditable,
                IM_COL32(gNoteColor.x * 255, gNoteColor.y * 255, gNoteColor.z * 255, 255));
        }

        {
            ImGui::SetNextWindowPos(ImVec2(0, midiH + 5), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(leftW, midH), ImGuiCond_FirstUseEver);
            ImGui::Begin("Settings", NULL, POSSIBLYEDITABLE);

            ImGui::Text("Window settings");
            if (ImGui::Checkbox("Always on top", (bool*)&alwaysontop))
                SDL_SetWindowAlwaysOnTop(window, (SDL_bool)alwaysontop);
            ImGui::Checkbox("Editable windows", (bool*)& windowsEditable);
            if (ImGui::Checkbox("Show titlebar", (bool*)&showTitlebar)) {
                SDL_SetWindowBordered(window, (SDL_bool)showTitlebar);
            }

            ImGui::Text("Opacity");
            if (ImGui::SliderInt("##opacity", &windowOpacity, 10, 100, "%d%%")) {
                logger.AddLog("Setting opacity to %d\n", windowOpacity);
                SDL_SetWindowOpacity(window, (float)windowOpacity / 100);
            }

            static bool showStyleEditor = false;
            if (ImGui::Button((!showStyleEditor ? "Open theme editor" : "Close theme editor")))
                showStyleEditor = !showStyleEditor;

            const char* layouts[] = { "Small", "Tall" };
            static const char* current_item = (smallLayout?"Small":"Tall");

            ImGui::Text("Layout");
            ImGui::PushItemWidth(ImGui::GetFontSize() * 6);
            if (ImGui::BeginCombo("##combo", current_item))
            {
                for (int n = 0; n < IM_ARRAYSIZE(layouts); n++)
                {
                    bool is_selected = (current_item == layouts[n]);
                    if (ImGui::Selectable(layouts[n], is_selected)) {
                        current_item = layouts[n];
                        if (current_item == std::string("Small")) {
                            SDL_SetWindowSize(window, 520, 420);
                            ImGui::LoadIniSettingsFromDisk("layout_small.ini");
                            smallLayout = true;
                        }
                        else if (current_item == std::string("Tall")) {
                            SDL_SetWindowSize(window, 520, 800);
                            ImGui::LoadIniSettingsFromDisk("layout_tall.ini");
                            smallLayout = false;
                        }
                    }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();

            if (showStyleEditor) {
                ImGui::Begin("Theme Editor");

                ImGui::ShowStyleEditor();
                ImGui::End();
            }

            ImGui::ColorEdit3("Background color", (float*)&gBackgroundColor, ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("Note color", (float*)&gNoteColor, ImGuiColorEditFlags_NoInputs);

            ImGui::Checkbox("RAINBOW MODE!", &rainbowMode);

            ImGui::Text("Piano settings");

            if (ImGui::Checkbox("Enable output", (bool*)&enableOutput)) {
                for (int i = 21; i <= 108; i++)
                    piano.up(i);
            }

            ImGui::Checkbox("88-key support", (bool*)& eightyeightkey);

            ImGui::Checkbox("Sustain", (bool*)& sustain);

            ImGui::Text("Sustain cutoff");
            ImGui::SliderInt("##sustain_cutoff", &sustainCutoff, 0, 127);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("CTRL + Click to enter a value,\ndefault is 64");
            }

            ImGui::Checkbox("Velocity", (bool*)& velocity);

            static bool foundDevice = false;
            auto did = Pm_GetDefaultInputDeviceID();
            if (did >= 0 && !foundDevice) {
                logger.AddLog("Opened MIDI device %s\n", Pm_GetDeviceInfo(did)->name);
                foundDevice = true;
            }

            // Target app selector — send keystrokes to a specific running app
            // instead of whatever is currently foregrounded. On macOS this also
            // bypasses the system dead-key / IME layer so modifier chords like
            // Option+digit arrive at the target as actual modifier chords.
            ImGui::Text("Target App");
            {
                static std::vector<IInputBackend::AppTarget> targets;
                static bool targetsLoaded = false;
                if (!targetsLoaded) {
                    targets = input->availableTargets();
                    targetsLoaded = true;
                }
                int curIdx = input->currentTargetIndex();
                const char* curName = (curIdx >= 0 && curIdx < (int)targets.size())
                                          ? targets[curIdx].name.c_str()
                                          : "(active foreground)";
                if (ImGui::BeginCombo("##target_app", curName)) {
                    // Refresh on every open so newly-launched apps appear.
                    targets = input->availableTargets();
                    curIdx = input->currentTargetIndex();
                    for (int i = 0; i < (int)targets.size(); ++i) {
                        bool selected = (curIdx == i);
                        if (ImGui::Selectable(targets[i].name.c_str(), selected)) {
                            input->setTargetIndex(i);
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Pick a running app to receive keystrokes.\n"
                        "(active foreground) sends wherever focus is.\n"
                        "On macOS, picking a specific app bypasses the\n"
                        "Option+digit dead-key composition so velocity\n"
                        "and other modifier chords work correctly.");
                }
            }

            // Input backend mode selector — backend-driven so Windows and macOS
            // expose their own mode lists without inline branching.
            ImGui::Text("QWERTY Emulator");
            const auto modes = input->availableModes();
            const auto tooltips = input->modeTooltips();
            if (qwertyEmulator < 0 || qwertyEmulator >= (int)modes.size()) qwertyEmulator = 0;
            if (ImGui::BeginCombo("Mode", modes[qwertyEmulator].c_str())) {
                for (int i = 0; i < (int)modes.size(); ++i) {
                    bool is_selected = (qwertyEmulator == i);
                    if (ImGui::Selectable(modes[i].c_str(), is_selected)) {
                        qwertyEmulator = i;
                        applyBackendMode();
                    }
                    if (i < (int)tooltips.size() && !tooltips[i].empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", tooltips[i].c_str());
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Allows you to play on alternate keyboard layouts,\n should be faster in practice for normal use");
            }

            ImGui::Text("MIDI Input");
            static const char* selectedDeviceName =
                (did >= 0) ? Pm_GetDeviceInfo(did)->name : "(no device)";
            // Keep the displayed label in sync if the user changed device via
            // the combo on a previous frame, or if a device appeared since startup.
            if (midi.deviceID >= 0) {
                selectedDeviceName = Pm_GetDeviceInfo(midi.deviceID)->name;
            }

            if (Pm_CountDevices() == 0) {
                ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.2f, 1.0f),
                    "No MIDI input devices detected.");
                ImGui::TextWrapped(
                    "Plug in a USB MIDI keyboard, or enable IAC Driver in "
                    "Audio MIDI Setup, then restart this app to see it here.");
            } else if (ImGui::BeginCombo("Port", selectedDeviceName)) {
                for (int i = 0; i < Pm_CountDevices(); i++) {
                    const PmDeviceInfo* deviceInfo = Pm_GetDeviceInfo(i);
                    if (!deviceInfo || deviceInfo->input == 0) continue;
                    bool is_selected = (midi.deviceID == i);

                    if (ImGui::Selectable(deviceInfo->name, is_selected)) {
                        std::cout << "Changed to " << deviceInfo->name << '\n';
                        selectedDeviceName = deviceInfo->name;
                        if (midi.stream) midi.shutdown(midi.stream);
                        midi.stream = nullptr;
                        midi.deviceID = i;
                        midi.InitWrapper();
                        logger.AddLog("Opened MIDI device %s\n", selectedDeviceName);
                    }

                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::Text("Reset");

            if (ImGui::Button("Reset settings")) {
                resetting = true;
            }

            if (resetting) {
                ImGui::PushItemWidth(ImGui::GetFontSize() * 7.0f);
                ImGui::SameLine();
                if (ImGui::Button("Yes")) {
                    printf("Resetting settings\n");
                    resetSettings();
                    resetting = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("No..."))
                    resetting = false;
            }

            ImGui::End();
        }

        // Player panel
        {
            ImGui::SetNextWindowPos(ImVec2(leftW + 5, midiH + 5), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(rightW, playerH), ImGuiCond_FirstUseEver);
            ImGui::Begin("Player", NULL, POSSIBLYEDITABLE);

            if (ImGui::BeginTabBar("##player_tabs")) {
            if (ImGui::BeginTabItem("Now Playing")) {
            static char pathBuf[1024] = {0};
            if (ImGui::Button("Browse...")) {
                std::string picked = platform::openMidiFileDialog(window);
                if (!picked.empty()) {
                    std::snprintf(pathBuf, sizeof(pathBuf), "%s", picked.c_str());
                    if (!filePlayer->load(pathBuf)) {
                        logger.AddLog("Player: failed to load %s\n", pathBuf);
                    }
                }
            }
            ImGui::SameLine();
            ImGui::PushItemWidth(-50.0f);
            ImGui::InputText("##player_path", pathBuf, sizeof(pathBuf));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Load") && pathBuf[0]) {
                if (!filePlayer->load(pathBuf)) {
                    logger.AddLog("Player: failed to load %s\n", pathBuf);
                }
            }

            const auto pstate = filePlayer->state();
            const char* stateLabel = "Empty";
            switch (pstate) {
                case MidiFilePlayer::State::Empty:   stateLabel = "Empty"; break;
                case MidiFilePlayer::State::Loaded:  stateLabel = "Loaded"; break;
                case MidiFilePlayer::State::Playing: stateLabel = "Playing"; break;
                case MidiFilePlayer::State::Paused:  stateLabel = "Paused"; break;
            }
            ImGui::Text("State: %s", stateLabel);
            if (!filePlayer->loadedPath().empty()) {
                // Show just the filename for compactness.
                const auto& fp = filePlayer->loadedPath();
                auto slash = fp.find_last_of("/\\");
                ImGui::TextWrapped("File: %s",
                    slash == std::string::npos ? fp.c_str() : fp.c_str() + slash + 1);
            }

            const bool canPlay = (pstate == MidiFilePlayer::State::Loaded ||
                                  pstate == MidiFilePlayer::State::Paused);
            const bool isPlaying = (pstate == MidiFilePlayer::State::Playing);

            if (ImGui::Button("Play") && canPlay)    filePlayer->play();
            ImGui::SameLine();
            if (ImGui::Button("Pause") && isPlaying) filePlayer->pause();
            ImGui::SameLine();
            if (ImGui::Button("Stop"))               filePlayer->stop();

            // Position / seek
            const double dur = filePlayer->durationSeconds();
            float pos = (float)filePlayer->positionSeconds();
            if (dur > 0) {
                ImGui::PushItemWidth(-1);
                if (ImGui::SliderFloat("##player_pos", &pos, 0.0f, (float)dur,
                                       "%.1fs", ImGuiSliderFlags_AlwaysClamp)) {
                    filePlayer->seek(pos);
                }
                ImGui::PopItemWidth();
                ImGui::Text("%.1fs / %.1fs", pos, dur);
            } else {
                ImGui::TextDisabled("(no file loaded)");
            }

            auto& cfg = filePlayer->config();
            ImGui::Checkbox("Loop", &cfg.loop);

            if (ImGui::CollapsingHeader("Mix")) {
                ImGui::SliderFloat("Tempo",    &cfg.tempoScale,    0.25f, 4.0f, "%.2fx");
                ImGui::SliderFloat("Velocity", &cfg.velocityScale, 0.25f, 4.0f, "%.2fx");
                ImGui::Checkbox("Apply CC 7 (volume) + CC 11 (expression)",
                                &cfg.applyChannelVolume);
            }
            if (ImGui::CollapsingHeader("Channels")) {
                // 16 channel toggles in a 4x4 grid.
                for (int ch = 0; ch < 16; ++ch) {
                    bool on = (cfg.channelMask & (uint16_t)(1u << ch)) != 0;
                    char lbl[8];
                    snprintf(lbl, sizeof(lbl), "%d", ch + 1);
                    if (ImGui::Checkbox(lbl, &on)) {
                        if (on) cfg.channelMask |= (uint16_t)(1u << ch);
                        else    cfg.channelMask &= (uint16_t)~(1u << ch);
                    }
                    if ((ch + 1) % 4 != 0) ImGui::SameLine();
                }
                ImGui::TextDisabled("Channel 10 (drumkit) is off by default.");
            }

            ImGui::EndTabItem();
            }

            // Shared between Library + Playlists tabs: the name of the playlist
            // that "Add to Playlist" actions should target.
            static std::string activePlaylistName;

            // ---- Library tab ---------------------------------------------
            if (ImGui::BeginTabItem("Library")) {
                static char rootBuf[1024] = {0};
                static char searchBuf[256] = {0};
                static int  selectedIdx = -1;

                // Sync the root buffer with whatever the library currently
                // believes its root is (e.g. after a startup scan or an
                // earlier Browse).
                if (rootBuf[0] == '\0' || library->currentRoot() != std::string(rootBuf)) {
                    std::snprintf(rootBuf, sizeof(rootBuf), "%s",
                                  library->currentRoot().c_str());
                }

                if (ImGui::Button("Browse##lib_root")) {
                    std::string picked = platform::openDirectoryDialog(
                        window, "Select MIDI library folder");
                    if (!picked.empty()) {
                        std::snprintf(rootBuf, sizeof(rootBuf), "%s", picked.c_str());
                        int n = library->scanDirectory(rootBuf);
                        logger.AddLog("Library: scanned %s (%d entries)\n", rootBuf, n);
                        selectedIdx = -1;
                    }
                }
                ImGui::SameLine();
                ImGui::PushItemWidth(-60.0f);
                ImGui::InputText("##lib_root", rootBuf, sizeof(rootBuf));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Scan")) {
                    int n = library->scanDirectory(rootBuf);
                    logger.AddLog("Library: scanned %s (%d entries)\n", rootBuf, n);
                    selectedIdx = -1;
                }

                ImGui::PushItemWidth(-1);
                ImGui::InputTextWithHint("##lib_search", "search...",
                                         searchBuf, sizeof(searchBuf));
                ImGui::PopItemWidth();

                const auto& all = library->entries();
                auto matches = library->search(searchBuf);
                ImGui::Text("%zu of %zu entries", matches.size(), all.size());

                // Action buttons for the current selection.
                if (selectedIdx >= 0 && (size_t)selectedIdx < all.size()) {
                    const auto& sel = all[(size_t)selectedIdx];
                    if (ImGui::Button("Load")) {
                        if (filePlayer->load(sel.path)) {
                            logger.AddLog("Library: loaded %s\n", sel.title.c_str());
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Load & Play")) {
                        if (filePlayer->load(sel.path)) {
                            filePlayer->play();
                            logger.AddLog("Library: playing %s\n", sel.title.c_str());
                        }
                    }
                    if (!activePlaylistName.empty()) {
                        ImGui::SameLine();
                        char addLbl[256];
                        std::snprintf(addLbl, sizeof(addLbl), "Add to '%s'",
                                      activePlaylistName.c_str());
                        if (ImGui::Button(addLbl)) {
                            if (auto* pl = playlists->find(activePlaylistName)) {
                                PlaylistEntry pe;
                                pe.libraryPath = sel.path;
                                pe.title = sel.title;
                                pl->entries.push_back(std::move(pe));
                                playlists->save(*pl);
                                logger.AddLog("Playlist '%s': added %s\n",
                                              activePlaylistName.c_str(), sel.title.c_str());
                            }
                        }
                    } else {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(no active playlist)");
                    }
                } else {
                    ImGui::TextDisabled("(select an entry)");
                }

                ImGui::BeginChild("##lib_list", ImVec2(0, 0), true);
                if (all.empty()) {
                    if (library->currentRoot().empty()) {
                        ImGui::TextWrapped("No library root set. Pick a folder with .mid files above.");
                    } else {
                        ImGui::TextWrapped("No MIDI files found under:\n%s",
                                           library->currentRoot().c_str());
                    }
                }
                for (size_t mIdx : matches) {
                    const auto& e = all[mIdx];
                    char label[768];
                    if (e.loadFailed) {
                        std::snprintf(label, sizeof(label),
                            "%s  (failed to parse)", e.title.c_str());
                    } else {
                        std::snprintf(label, sizeof(label),
                            "%s  %d:%02d  %d notes",
                            e.title.c_str(),
                            (int)(e.duration / 60.0),
                            (int)e.duration % 60,
                            e.noteCount);
                    }
                    bool isSel = (selectedIdx == (int)mIdx);
                    if (ImGui::Selectable(label, isSel,
                                          ImGuiSelectableFlags_AllowDoubleClick)) {
                        selectedIdx = (int)mIdx;
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            if (filePlayer->load(e.path)) {
                                filePlayer->play();
                                logger.AddLog("Library: playing %s\n", e.title.c_str());
                            }
                        }
                    }
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }

            // ---- Playlists tab -------------------------------------------
            if (ImGui::BeginTabItem("Playlists")) {
                auto& all = playlists->playlists();

                // Active-playlist selector + create / delete.
                static char newNameBuf[128] = {0};
                const char* curName = activePlaylistName.empty()
                    ? "(none)" : activePlaylistName.c_str();
                ImGui::PushItemWidth(-180.0f);
                if (ImGui::BeginCombo("##pl_select", curName)) {
                    if (ImGui::Selectable("(none)", activePlaylistName.empty())) {
                        activePlaylistName.clear();
                    }
                    for (auto& p : all) {
                        bool sel = (activePlaylistName == p.name);
                        if (ImGui::Selectable(p.name.c_str(), sel)) {
                            activePlaylistName = p.name;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Delete") && !activePlaylistName.empty()) {
                    playlists->remove(activePlaylistName);
                    activePlaylistName.clear();
                }

                ImGui::PushItemWidth(-90.0f);
                ImGui::InputTextWithHint("##pl_new", "new playlist name",
                                         newNameBuf, sizeof(newNameBuf));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Create") && newNameBuf[0]) {
                    if (auto* p = playlists->createNew(newNameBuf)) {
                        activePlaylistName = p->name;
                        newNameBuf[0] = '\0';
                    }
                }

                // Active playlist body.
                Playlist* active = activePlaylistName.empty()
                    ? nullptr : playlists->find(activePlaylistName);
                if (active) {
                    ImGui::Separator();
                    ImGui::Text("Tracks (%zu)", active->entries.size());
                    if (active->entries.empty()) {
                        ImGui::TextDisabled("Empty. Use the Library tab and "
                                            "click 'Add to ...' on entries.");
                    }
                    int removeIdx = -1;
                    for (size_t i = 0; i < active->entries.size(); ++i) {
                        auto& e = active->entries[i];
                        ImGui::PushID((int)i);
                        char buf[640];
                        std::snprintf(buf, sizeof(buf), "%zu. %s",
                                      i + 1,
                                      e.title.empty() ? e.libraryPath.c_str() : e.title.c_str());
                        ImGui::Selectable(buf);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("^") && i > 0) {
                            std::swap(active->entries[i], active->entries[i-1]);
                            playlists->save(*active);
                            ImGui::PopID();
                            continue;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("v") && i + 1 < active->entries.size()) {
                            std::swap(active->entries[i], active->entries[i+1]);
                            playlists->save(*active);
                            ImGui::PopID();
                            continue;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("x")) removeIdx = (int)i;
                        ImGui::PopID();
                    }
                    if (removeIdx >= 0) {
                        active->entries.erase(active->entries.begin() + removeIdx);
                        playlists->save(*active);
                    }

                    // Concert controls.
                    ImGui::Separator();
                    auto& ccfg = concert->config();
                    ImGui::SliderFloat("Gap (s)", &ccfg.gapSeconds, 0.0f, 30.0f, "%.1f");
                    const char* loopLabels[] = {"Off", "Repeat all", "Repeat one", "Shuffle"};
                    int loopIdx = (int)ccfg.loopMode;
                    if (ImGui::Combo("Loop", &loopIdx, loopLabels, IM_ARRAYSIZE(loopLabels))) {
                        ccfg.loopMode = (Concert::LoopMode)loopIdx;
                    }

                    if (concert->isRunning()) {
                        if (ImGui::Button("Stop concert")) concert->stop();
                        ImGui::SameLine();
                        if (ImGui::Button("Prev"))         concert->previous();
                        ImGui::SameLine();
                        if (ImGui::Button("Next"))         concert->next();
                        const auto& cp = concert->currentPlaylist();
                        size_t idx = concert->currentIndex();
                        if (idx < cp.entries.size()) {
                            ImGui::Text("Playing %zu / %zu — %s",
                                idx + 1, cp.entries.size(),
                                cp.entries[idx].title.empty()
                                    ? cp.entries[idx].libraryPath.c_str()
                                    : cp.entries[idx].title.c_str());
                        }
                    } else {
                        bool canPlay = !active->entries.empty();
                        if (canPlay) {
                            if (ImGui::Button("Play All (concert)")) concert->start(*active);
                        } else {
                            ImGui::TextDisabled("(add tracks above to enable concert)");
                        }
                    }
                    ImGui::TextDisabled("Panic hotkey: Cmd+Shift+P (Mac) / Ctrl+Alt+P (Win)");
                } else {
                    ImGui::TextWrapped(
                        "Pick or create a playlist above, then add tracks from "
                        "the Library tab.");
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
            }

            ImGui::End();
        }

        {
            ImGui::SetNextWindowPos(ImVec2(leftW + 5, midiH + 10 + playerH), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(rightW, logH), ImGuiCond_FirstUseEver);
            ImGui::Begin("Log", NULL, POSSIBLYEDITABLE);
            ImGui::End();

            logger.Draw("Log", &show_log_window);
        }

        static bool firstDown = true;
        static int relOldMousePos[2];
        if (rightDown) {
            int x, y;
            int winx, winy;
            int sizex, sizey;
            SDL_GetWindowPosition(window, &winx, &winy);
            SDL_GetWindowSize(window, &sizex, &sizey);
            SDL_GetMouseState(&x, &y);
            x -= sizex / 2;
            y -= sizey / 2;
            if (firstDown) {
                SDL_GetMouseState(&relOldMousePos[0], &relOldMousePos[1]);
                firstDown = false;
            }
            SDL_SetWindowPosition(window, winx + x, winy + y);
        }

        // Limit the FPS to 100
        SDL_Delay(10);

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int) io.DisplaySize.x, (int) io.DisplaySize.y);
        glClearColor(gBackgroundColor.x, gBackgroundColor.y, gBackgroundColor.z, gBackgroundColor.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    settingsHandler.DumpSettings();

    // Cleanup
    midiThreadExitSignal.set_value();
    midithread.join();
    if (concert)    concert->stop();
    if (filePlayer) filePlayer->stop();
    if (noteRouter) noteRouter->releaseAll();
    concert.reset();
    filePlayer.reset();  // join scheduler thread before backend/router die
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
