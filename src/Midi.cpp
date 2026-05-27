//
// Created by Chris Young on 22/4/20.

#include <iostream>
#include <cstdlib>

#include <SDL.h>

#include "Midi.h"

template<typename T>
void must(T err) {
    if (err != 0) {
        std::string errorText = Pm_GetErrorText(static_cast<PmError>(err));
        std::string errorTip = "";

        if (errorText == "PortMidi: Bad pointer")
            errorTip = "Error: Your MIDI input port couldn't be found.";
        else if (errorText == "PortMidi: Host error")
            errorTip = "Error: Your MIDI input port is being used by another program.";
        else
        {
            errorTip = "Error: " + errorText;
        }

        fprintf(stderr, "Error occurred: %s\n", errorTip.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 errorText.c_str(),
                                 errorTip.c_str(),
                                 nullptr);

        std::exit(1);
    }
}

PmDeviceID init() {
    must(Pm_Initialize());
    if (!Pt_Started()) Pt_Start(1, nullptr, nullptr);
    auto did = Pm_GetDefaultInputDeviceID();
    if (did < 0) {
        std::cout << "Couldn't find any MIDI devices for input."
                  << std::endl;
        return did;
    }
    int count = Pm_CountDevices();
    for (int id = 0; id < count; id++) {
        auto di = Pm_GetDeviceInfo(id);
        std::cout << di->interf << "/" << di->name
                  << ", input: " << di->input
                  << ", output: " << di->output
                  << ", opened: " << di->opened;
        if (id == did) {
            std::cout << " (DEFAULT)";
        }
        std::cout << std::endl;
    }
    return did;
}

void Midi::InitWrapper() {
    stream = nullptr;
    if (deviceID < 0) return;
    must(Pm_OpenInput(&stream, deviceID, nullptr, 1024, nullptr, nullptr));
    init();
}

void Midi::shutdown(PortMidiStream *s) {
    // Cleanup path: best-effort, don't tear the app down on residual errors.
    if (s) Pm_Close(s);
    if (Pt_Started()) Pt_Stop();
}


Midi::Midi(PmDeviceID passedID) {
    deviceID = init(); // this gets default
    if (passedID >= 0) deviceID = passedID; // if we got passed an ID

    stream = nullptr;
    if (deviceID >= 0) {
        must(Pm_OpenInput(&stream, deviceID, nullptr, 1024, nullptr, nullptr));
    }
    // deviceID < 0 is allowed: the UI exposes a device picker, and the
    // built-in file player (Stage 2) drives the same pollCallback pipeline
    // without needing a hardware input.
}

void Midi::poll(std::function<void(PmTimestamp, uint8_t, PmMessage, PmMessage)> callback, bool debug) {
    if (!stream) return; // no device attached → nothing to read
    PmError err = Pm_Poll(stream);
    if (err > 0) {
        int count = Pm_Read(stream, buffer, 1024);
        if (count > 0) {
            for (int ev = 0; ev < count; ev++) {
                PmTimestamp timestamp = buffer[ev].timestamp;
                PmMessage message = buffer[ev].message;
                uint8_t Status = ((uint32_t) message & 0xFFu);
                PmMessage Data1 = (((uint32_t) message >> 8) & 0xFFu);
                PmMessage Data2 = ((uint32_t) message >> 16 & 0xFFu);
                if (Data1 || Data2) {
                    callback(timestamp, Status, Data1, Data2);
                }
            }
        }
    }
}

Midi::~Midi() {
    shutdown(stream);
}
