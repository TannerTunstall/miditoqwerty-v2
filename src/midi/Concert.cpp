#include "Concert.h"

#include "Log.h"
#include "MidiFilePlayer.h"

using clock_t_ = std::chrono::steady_clock;

Concert::Concert(MidiFilePlayer* p, Log* l) : player(p), logger(l) {}

void Concert::start(const Playlist& pl) {
    active = pl;
    currentIdx = 0;
    inGap = false;
    running = !active.entries.empty();
    if (running) {
        if (logger) logger->AddLog("Concert: started '%s' (%zu tracks)\n",
                                   active.name.c_str(), active.entries.size());
        loadAndPlay(currentIdx);
    } else if (logger) {
        logger->AddLog("Concert: start failed, playlist '%s' is empty\n",
                       active.name.c_str());
    }
}

void Concert::stop() {
    if (!running && !inGap) return;
    running = false;
    inGap = false;
    if (player) player->stop();
    if (logger) logger->AddLog("Concert: stopped\n");
}

void Concert::next() {
    if (active.entries.empty()) return;
    std::size_t target;
    if (cfg.loopMode == LoopMode::Shuffle) {
        target = pickShuffleIndex();
    } else {
        target = (currentIdx + 1) % active.entries.size();
    }
    inGap = false;
    loadAndPlay(target);
}

void Concert::previous() {
    if (active.entries.empty()) return;
    std::size_t target = currentIdx == 0
        ? active.entries.size() - 1
        : currentIdx - 1;
    inGap = false;
    loadAndPlay(target);
}

void Concert::tick() {
    if (!running || !player) return;

    if (inGap) {
        const double elapsed = std::chrono::duration<double>(
            clock_t_::now() - gapStart).count();
        if (elapsed >= cfg.gapSeconds) {
            inGap = false;
            std::size_t nextIdx;
            switch (cfg.loopMode) {
                case LoopMode::RepeatOne:
                    nextIdx = currentIdx;
                    break;
                case LoopMode::Shuffle:
                    nextIdx = pickShuffleIndex();
                    break;
                case LoopMode::RepeatAll:
                    nextIdx = (currentIdx + 1) % active.entries.size();
                    break;
                case LoopMode::Off:
                default:
                    if (currentIdx + 1 >= active.entries.size()) {
                        running = false;
                        if (logger) logger->AddLog("Concert: playlist exhausted\n");
                        return;
                    }
                    nextIdx = currentIdx + 1;
                    break;
            }
            loadAndPlay(nextIdx);
        }
        return;
    }

    // Detect player going from Playing back to Loaded — that's "track ended"
    // (the scheduler resets position to 0 and drops to Loaded when done).
    static MidiFilePlayer::State previousState = MidiFilePlayer::State::Empty;
    const auto state = player->state();
    if (previousState == MidiFilePlayer::State::Playing &&
        state == MidiFilePlayer::State::Loaded) {
        // Begin inter-track gap.
        if (cfg.gapSeconds > 0.001f) {
            inGap = true;
            gapStart = clock_t_::now();
        } else {
            // Zero gap: advance immediately.
            std::size_t nextIdx;
            switch (cfg.loopMode) {
                case LoopMode::RepeatOne: nextIdx = currentIdx; break;
                case LoopMode::Shuffle:   nextIdx = pickShuffleIndex(); break;
                case LoopMode::RepeatAll: nextIdx = (currentIdx + 1) % active.entries.size(); break;
                case LoopMode::Off:
                default:
                    if (currentIdx + 1 >= active.entries.size()) {
                        running = false;
                        if (logger) logger->AddLog("Concert: playlist exhausted\n");
                        previousState = state;
                        return;
                    }
                    nextIdx = currentIdx + 1;
                    break;
            }
            loadAndPlay(nextIdx);
        }
    }
    previousState = state;
}

void Concert::loadAndPlay(std::size_t idx) {
    if (idx >= active.entries.size() || !player) return;
    currentIdx = idx;
    const auto& e = active.entries[idx];
    if (player->load(e.libraryPath)) {
        // Apply per-entry overrides on top of the player's live config. The
        // player config persists, so reset to 1.0 to avoid compounding when a
        // user has a non-default mix already set.
        auto& pcfg = player->config();
        pcfg.tempoScale    = e.tempoOverride;
        pcfg.velocityScale = e.velocityOverride;
        player->play();
        if (logger) logger->AddLog("Concert: now playing '%s' (%zu/%zu)\n",
                                   e.title.empty() ? e.libraryPath.c_str() : e.title.c_str(),
                                   idx + 1, active.entries.size());
    } else if (logger) {
        logger->AddLog("Concert: load failed for '%s', skipping\n",
                       e.libraryPath.c_str());
        // Skip the broken track so the concert doesn't stall.
        if (idx + 1 < active.entries.size()) {
            loadAndPlay(idx + 1);
        } else {
            running = false;
        }
    }
}

std::size_t Concert::pickShuffleIndex() {
    if (active.entries.size() == 1) return 0;
    std::uniform_int_distribution<std::size_t> dist(0, active.entries.size() - 1);
    std::size_t pick;
    do {
        pick = dist(rng);
    } while (pick == lastShuffleIdx);
    lastShuffleIdx = pick;
    return pick;
}
