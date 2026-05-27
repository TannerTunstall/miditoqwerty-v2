#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class NoteRouter;
class Piano;
class Log;

// Plays .mid files through the same NoteRouter → IInputBackend pipeline that
// live PortMidi input uses. Owns a dedicated scheduler thread that sleeps
// until the next event, then dispatches it tempo-accurately. The scheduler
// thread is created once at construction and parked on a condition variable
// until the user hits Play.
//
// Tempo (FF 51 03) and time-signature changes inside the SMF are honoured by
// midifile's doTimeAnalysis(), which gives every event an absolute `seconds`
// timestamp. Our scheduler just chases wall-clock time against that.
class MidiFilePlayer {
public:
    enum class State { Empty, Loaded, Playing, Paused };

    struct Config {
        // 16-bit bitmask; bit N set ⇒ channel N+1 feeds output.
        // Default: every channel except 10 (drumkit), index 9.
        uint16_t channelMask = (uint16_t)~(1u << 9);
        // 1.0 = original tempo. 2.0 = 2x faster. Clamp at the UI.
        float    tempoScale  = 1.0f;
        // Multiplies every NoteOn velocity before it hits the router. The
        // router still buckets into Virtual Piano's 32 velocity steps.
        float    velocityScale = 1.0f;
        // Treat Channel Volume (CC 7) + Expression (CC 11) as a multiplier
        // on subsequent NoteOn velocities for that channel. Default on.
        bool     applyChannelVolume = true;
        bool     loop = false;
    };

    MidiFilePlayer(NoteRouter* router, Piano* piano, Log* logger);
    ~MidiFilePlayer();

    bool load(const std::string& path);

    void play();
    void pause();
    void stop();          // releases all held keys and rewinds to 0
    void seek(double seconds);

    State       state() const            { return currentState.load(); }
    const std::string& loadedPath() const { return loadedFilePath; }
    double      durationSeconds() const  { return totalDuration; }
    double      positionSeconds() const  { return currentPosition.load(); }

    Config&     config() { return cfg; }   // UI mutates directly

private:
    struct Event {
        double  timeSeconds;  // absolute, post-tempo-analysis
        uint8_t status;       // includes channel nibble
        uint8_t data1;
        uint8_t data2;
        uint8_t channel;      // status & 0x0F, cached
    };

    void schedulerLoop();
    void dispatch(const Event& e);
    void releaseAllHeld();

    NoteRouter* router;
    Piano*      piano;
    Log*        logger;

    std::vector<Event> events;        // sorted by timeSeconds
    double             totalDuration = 0.0;
    std::string        loadedFilePath;

    // Per-channel last-seen CC 7 / CC 11. Applied as velocity gain.
    uint8_t volumeCC[16] = {127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127};
    uint8_t expressionCC[16] = {127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127};

    Config cfg;

    // Scheduler state.
    std::thread             worker;
    std::mutex              mutex;
    std::condition_variable cv;
    std::atomic<State>      currentState{State::Empty};
    std::atomic<double>     currentPosition{0.0};
    std::atomic<bool>       quit{false};
    std::atomic<bool>       seekRequested{false};
    double                  seekTarget = 0.0;
};
