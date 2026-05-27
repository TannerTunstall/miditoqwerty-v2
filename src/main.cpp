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
#include "core/NoteRouter.h"

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

ImVec4 gBackgroundColor;
ImVec4 gNoteColor;
ImVec4 gNoteNameColor;

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
    if (isNoteOn)  piano.down((int)Data1, (int)Data2);
    if (isNoteOff) piano.up((int)Data1);

    if (noteRouter) noteRouter->onMidiEvent(timestamp, status, Data1, Data2);
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

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
        std::exit(2);
    }

    fflush(stdout);

    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL        |
                                                     SDL_WINDOW_ALLOW_HIGHDPI |
                                                     SDL_WINDOW_ALWAYS_ON_TOP );
    window = SDL_CreateWindow("Midi to Qwerty", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 435, 550, window_flags);
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

    // Load fonts
    for (auto& p : std::filesystem::recursive_directory_iterator("fonts"))
    {
        if (p.path().extension() == ".ttf") {

            std::string relativePath = p.path().stem().string();
            if (relativePath == defaultFont) continue;

            std::string fullPath = "fonts/" + relativePath + ".ttf";

            if (fullPath == initializedFont) continue;

            printf("Font found: %s\n", fullPath.c_str());

            ImGui::GetIO().Fonts->AddFontFromFileTTF(fullPath.c_str(), 13);
        }
    }

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
        SDL_SetWindowSize(window, 435, 315);
        printf("Loaded small layout\n");
    }
    else {
        SDL_SetWindowSize(window, 435, 550);
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

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();
        if (show_midi_window) {
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
            piano.draw(&show_piano_window, windowsEditable,
                IM_COL32(gNoteColor.x * 255, gNoteColor.y * 255, gNoteColor.z * 255, 255));
        }

        {
            ImGui::Begin("Settings", NULL, POSSIBLYEDITABLE);

            ImGui::Text("Window settings");
            if (ImGui::Checkbox("Always on top", (bool*)&alwaysontop))
                SDL_SetWindowAlwaysOnTop(window, (SDL_bool)alwaysontop);
            ImGui::Checkbox("Editable windows", (bool*)& windowsEditable);
            if (ImGui::Checkbox("Show titlebar", (bool*)&showTitlebar)) {
                SDL_SetWindowBordered(window, (SDL_bool)showTitlebar);
            }

            ImGui::Text("Opacity");
            if (ImGui::SliderInt("##", &windowOpacity, 10, 100, "%d%%")) {
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
                            SDL_SetWindowSize(window, 435, 310);
                            ImGui::LoadIniSettingsFromDisk("layout_small.ini");
                            smallLayout = true;
                        }
                        else if (current_item == std::string("Tall")) {
                            SDL_SetWindowSize(window, 435, 550);
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
            ImGui::SliderInt("", &sustainCutoff, 0, 127);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("CTRL + Click to enter a value,\ndefault is 64");
            }

            ImGui::Checkbox("Velocity", (bool*)& velocity);

            static bool foundDevice = false;
            auto did = Pm_GetDefaultInputDeviceID();
            if (did < 0) {
                std::cout << "Couldn't find any MIDI devices for input."
                    << std::endl;
                return did;
            }
            else if(!foundDevice) {
                logger.AddLog("Opened MIDI device %s\n", Pm_GetDeviceInfo(did)->name);
                foundDevice = true;
            }

            // Input backend mode selector — backend-driven so Windows and macOS
            // expose their own mode lists without inline branching.
            ImGui::Text("QWERTY Emulator");
            const auto modes = input->availableModes();
            if (qwertyEmulator < 0 || qwertyEmulator >= (int)modes.size()) qwertyEmulator = 0;
            if (ImGui::BeginCombo("Mode", modes[qwertyEmulator].c_str())) {
                for (int i = 0; i < (int)modes.size(); ++i) {
                    bool is_selected = (qwertyEmulator == i);
                    if (ImGui::Selectable(modes[i].c_str(), is_selected)) {
                        qwertyEmulator = i;
                        applyBackendMode();
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Allows you to play on alternate keyboard layouts,\n should be faster in practice for normal use");
            }

            ImGui::Text("MIDI Input");
            PmDeviceID selectedDevice = did;
            static const char* selectedDeviceName = Pm_GetDeviceInfo(did)->name;
            if (ImGui::BeginCombo("Port", selectedDeviceName)) {
                for (int i = 0; i < Pm_CountDevices() - 1; i++) {
                    const PmDeviceInfo* deviceInfo = Pm_GetDeviceInfo(i);
                    if (deviceInfo->input == 0) continue;
                    bool is_selected = (selectedDeviceName == Pm_GetDeviceInfo(selectedDevice)->name);

                    if (ImGui::Selectable(deviceInfo->name, is_selected)) {
                        std::cout << "Changed to " << deviceInfo->name << '\n';
                        selectedDeviceName = deviceInfo->name;
                        midi.shutdown(midi.stream);
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

        {
            ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
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
    if (noteRouter) noteRouter->releaseAll();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
