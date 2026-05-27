#include "MidiFilePlayer.h"

#include <algorithm>
#include <chrono>

#include "MidiFile.h"   // craigsapp/midifile

#include "Log.h"
#include "Piano.h"
#include "core/NoteRouter.h"

using clock_t_ = std::chrono::steady_clock;

MidiFilePlayer::MidiFilePlayer(NoteRouter* r, Piano* p, Log* l)
    : router(r), piano(p), logger(l) {
    worker = std::thread([this]{ schedulerLoop(); });
}

MidiFilePlayer::~MidiFilePlayer() {
    {
        std::lock_guard<std::mutex> lk(mutex);
        quit.store(true);
        currentState.store(State::Empty);
    }
    cv.notify_all();
    if (worker.joinable()) worker.join();
    releaseAllHeld();
}

bool MidiFilePlayer::load(const std::string& path) {
    smf::MidiFile mf;
    if (!mf.read(path)) {
        if (logger) logger->AddLog("MidiFilePlayer: failed to read %s\n", path.c_str());
        return false;
    }
    mf.doTimeAnalysis();

    // Drain any in-flight playback before swapping in the new event list.
    {
        std::lock_guard<std::mutex> lk(mutex);
        currentState.store(State::Empty);
    }
    cv.notify_all();
    releaseAllHeld();

    std::vector<Event> next;
    next.reserve(1024);
    for (int t = 0; t < mf.getTrackCount(); ++t) {
        for (int i = 0; i < mf[t].size(); ++i) {
            auto& ev = mf[t][i];
            if (ev.isMeta()) continue;  // tempo/timesig already folded into ev.seconds
            if (ev.size() == 0) continue;
            Event pe;
            pe.timeSeconds = ev.seconds;
            pe.status      = (uint8_t)(int)ev[0];
            pe.data1       = ev.size() > 1 ? (uint8_t)(int)ev[1] : 0;
            pe.data2       = ev.size() > 2 ? (uint8_t)(int)ev[2] : 0;
            pe.channel     = (uint8_t)(pe.status & 0x0F);
            next.push_back(pe);
        }
    }
    std::stable_sort(next.begin(), next.end(),
        [](const Event& a, const Event& b){ return a.timeSeconds < b.timeSeconds; });

    {
        std::lock_guard<std::mutex> lk(mutex);
        events = std::move(next);
        totalDuration = events.empty() ? 0.0 : events.back().timeSeconds;
        loadedFilePath = path;
        currentPosition.store(0.0);
        // reset volume/expression to defaults — they're re-sent by the file
        for (int c = 0; c < 16; ++c) { volumeCC[c] = 127; expressionCC[c] = 127; }
        currentState.store(State::Loaded);
    }
    if (logger) logger->AddLog("MidiFilePlayer: loaded %s (%zu events, %.2fs)\n",
                               path.c_str(), events.size(), totalDuration);
    return true;
}

void MidiFilePlayer::play() {
    {
        std::lock_guard<std::mutex> lk(mutex);
        if (events.empty()) return;
        currentState.store(State::Playing);
    }
    cv.notify_all();
}

void MidiFilePlayer::pause() {
    {
        std::lock_guard<std::mutex> lk(mutex);
        if (currentState.load() == State::Playing) currentState.store(State::Paused);
    }
    cv.notify_all();
    releaseAllHeld();
}

void MidiFilePlayer::stop() {
    {
        std::lock_guard<std::mutex> lk(mutex);
        currentState.store(events.empty() ? State::Empty : State::Loaded);
        currentPosition.store(0.0);
    }
    cv.notify_all();
    releaseAllHeld();
}

void MidiFilePlayer::seek(double seconds) {
    {
        std::lock_guard<std::mutex> lk(mutex);
        seekTarget = std::clamp(seconds, 0.0, totalDuration);
        seekRequested.store(true);
    }
    cv.notify_all();
    releaseAllHeld();
}

void MidiFilePlayer::releaseAllHeld() {
    // Best-effort cleanup: NoteRouter's own releaseAll drops the sustain pedal
    // and asks the backend to release any held modifiers. Per-note keyups for
    // notes that were actively held at the moment of seek/pause/stop would
    // require us to track them here — that's a Stage-3 polish.
    if (router) router->releaseAll();
}

void MidiFilePlayer::dispatch(const Event& e) {
    const uint8_t cmd = (uint8_t)(e.status & 0xF0);

    // Track CC 7 / CC 11 per channel for velocity scaling.
    if (cmd == 0xB0) {
        if (e.data1 == 7)  volumeCC[e.channel]     = e.data2;
        if (e.data1 == 11) expressionCC[e.channel] = e.data2;
    }

    // Channel filter.
    if (!(cfg.channelMask & (uint16_t)(1u << e.channel))) {
        // Sustain CC and similar still pass through if the channel is muted?
        // Decision: no — mute means mute everything for that channel.
        return;
    }

    uint8_t outData2 = e.data2;
    if (cmd == 0x90 && e.data2 != 0) {
        // NoteOn: scale velocity by CC 7, CC 11, and the per-player gain.
        float scaled = (float)e.data2;
        if (cfg.applyChannelVolume) {
            scaled *= (volumeCC[e.channel] / 127.0f) * (expressionCC[e.channel] / 127.0f);
        }
        scaled *= cfg.velocityScale;
        if (scaled > 127.0f) scaled = 127.0f;
        if (scaled < 1.0f)   scaled = 1.0f;
        outData2 = (uint8_t)scaled;

        if (piano) piano->down((int)e.data1, (int)outData2);
    } else if (cmd == 0x80) {
        if (piano) piano->up((int)e.data1);
    }

    if (router) router->onMidiEvent(0, e.status, e.data1, outData2);
}

void MidiFilePlayer::schedulerLoop() {
    std::unique_lock<std::mutex> lock(mutex);

    while (!quit.load()) {
        cv.wait(lock, [&]{
            return quit.load() || currentState.load() == State::Playing;
        });
        if (quit.load()) return;

        // Advance index to the first event at-or-after currentPosition.
        size_t idx = 0;
        const double startPos = currentPosition.load();
        while (idx < events.size() && events[idx].timeSeconds < startPos) ++idx;

        auto wallStart = clock_t_::now();
        double basePos = startPos;

        while (currentState.load() == State::Playing && idx < events.size() && !quit.load()) {
            if (seekRequested.exchange(false)) {
                basePos = seekTarget;
                currentPosition.store(basePos);
                wallStart = clock_t_::now();
                idx = 0;
                while (idx < events.size() && events[idx].timeSeconds < basePos) ++idx;
                continue;
            }

            const double target = events[idx].timeSeconds;
            const double playSecs = std::chrono::duration<double>(clock_t_::now() - wallStart).count();
            const double targetPlay = (target - basePos) / std::max(0.001f, cfg.tempoScale);
            const double sleepFor = targetPlay - playSecs;

            if (sleepFor > 0.0005) {
                // Wake early on state change (pause/stop/seek). Lock is released
                // during wait per cv contract.
                cv.wait_for(lock, std::chrono::duration<double>(sleepFor));
                continue;
            }

            // Time to fire this event. Drop the lock so dispatch doesn't hold
            // it while calling into NoteRouter (which has its own mutex).
            const Event e = events[idx];
            currentPosition.store(target);
            lock.unlock();
            dispatch(e);
            lock.lock();
            ++idx;
        }

        if (idx >= events.size() && currentState.load() == State::Playing) {
            // Reached the end.
            releaseAllHeld();
            if (cfg.loop) {
                currentPosition.store(0.0);
                // Stay in Playing — loop back through outer while.
            } else {
                currentState.store(State::Loaded);
                currentPosition.store(0.0);
            }
        }
    }
}
