#pragma once
#include <chrono>
#include <cstddef>
#include <random>

#include "Playlist.h"

class MidiFilePlayer;
class Log;

// Drives autoplay of a Playlist through a MidiFilePlayer. Owns the
// "currently playing index", inter-track gap timing, and the loop/shuffle
// policy. Stateless w.r.t. file loading — defers entirely to MidiFilePlayer.
//
// Called once per UI frame via tick(). When the underlying player finishes
// a track (its state transitions out of Playing without a loop set on the
// player itself), Concert waits gapSeconds then advances to the next track
// per loopMode. Stops cleanly when the playlist is exhausted (Off mode).
class Concert {
public:
    enum class LoopMode { Off, RepeatAll, RepeatOne, Shuffle };

    struct Config {
        float    gapSeconds = 2.0f;
        LoopMode loopMode   = LoopMode::Off;
    };

    Concert(MidiFilePlayer* player, Log* logger);

    // Begin playing a playlist. Resets the index; loads + plays entry 0.
    // Subsequent calls replace the current playlist mid-concert.
    void start(const Playlist& pl);

    // Halt the concert and the underlying player (the player itself will
    // release any held keys via NoteRouter::releaseAll).
    void stop();

    // Skip immediately to the next/previous track per loopMode.
    void next();
    void previous();

    // Polled from the main loop each frame.
    void tick();

    bool        isRunning()    const { return running; }
    std::size_t currentIndex() const { return currentIdx; }
    const Playlist& currentPlaylist() const { return active; }
    Config&     config()              { return cfg; }

private:
    void loadAndPlay(std::size_t idx);
    std::size_t pickShuffleIndex();

    MidiFilePlayer* player;
    Log*            logger;
    Playlist        active;
    Config          cfg;
    std::size_t     currentIdx = 0;
    bool            running    = false;

    // Inter-track gap timer
    bool            inGap = false;
    std::chrono::steady_clock::time_point gapStart;

    // Shuffle history so we don't repeat the same track immediately
    std::mt19937    rng{std::random_device{}()};
    std::size_t     lastShuffleIdx = (std::size_t)-1;
};
