#pragma once
#include <cstdint>
#include <portmidi.h>

class IInputBackend;
class Log;

// Translates incoming MIDI events into Virtual Piano keystroke calls on the
// input backend. Owns the small pieces of routing state (sustain held,
// last-emitted velocity bucket) so the main loop can be drained of globals.
//
// Thread model: onMidiEvent() is called from the MIDI poll thread. Settings
// (pointed to by the int*'s) are mutated by the UI thread. Torn int reads are
// considered acceptable, matching the original behaviour — fixing the race is
// out of scope for Stage 1.2.
class NoteRouter {
public:
    NoteRouter(IInputBackend* backend, Log* logger);

    void bindSettings(int* enableOutput,
                      int* eightyEightKey,
                      int* sustain,
                      int* velocity,
                      int* sustainCutoff);

    void onMidiEvent(PmTimestamp ts, uint8_t status, uint8_t data1, uint8_t data2);

    // Releases sustain + asks the backend to drop any stuck modifiers. Safe to
    // call from any thread.
    void releaseAll();

private:
    IInputBackend* backend;
    Log* logger;

    int* pEnableOutput   = nullptr;
    int* pEightyEightKey = nullptr;
    int* pSustain        = nullptr;
    int* pVelocity       = nullptr;
    int* pSustainCutoff  = nullptr;

    bool sustainOn = false;
    char prevVelocityChar = 'X';
};
