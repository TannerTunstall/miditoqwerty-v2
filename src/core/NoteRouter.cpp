#include "NoteRouter.h"

#include <cstdlib>
#include <future>

#include "Log.h"
#include "Qwerty.h"
#include "platform/IInputBackend.h"

NoteRouter::NoteRouter(IInputBackend* backend_, Log* logger_)
    : backend(backend_), logger(logger_) {}

void NoteRouter::bindSettings(int* enableOutput, int* eightyEightKey,
                              int* sustain, int* velocity, int* sustainCutoff) {
    pEnableOutput   = enableOutput;
    pEightyEightKey = eightyEightKey;
    pSustain        = sustain;
    pVelocity       = velocity;
    pSustainCutoff  = sustainCutoff;
}

namespace {
// Preserve the original std::async-fire-and-discard pattern so behaviour
// matches the legacy pollCallback verbatim. Note: with std::launch::async the
// returned future's destructor *blocks* until the task completes, so these are
// effectively synchronous calls dispatched on a fresh thread. Documented here
// because the original author probably did not intend that; cleanup is left
// for a later pass to keep this commit zero-behaviour-change.
template <class Fn, class... Args>
void fireAndDiscard(Fn&& fn, Args&&... args) {
    std::async(std::launch::async, std::forward<Fn>(fn), std::forward<Args>(args)...);
}
}  // namespace

void NoteRouter::onMidiEvent(PmTimestamp /*ts*/, uint8_t status,
                             uint8_t data1, uint8_t data2) {
    std::lock_guard<std::mutex> lk(mutex);

    if (logger) logger->AddLog("Event status: %d, Data1: %04X, Data2: %04X\n",
                               status, data1, data2);

    if (!pEnableOutput || !*pEnableOutput) return;

    const bool isNoteOn        = (status >= 0x90 && status <= 0x9F);
    const bool isNoteOff       = (status >= 0x80 && status <= 0x8F);
    const bool isControlChange = (status >= 0xB0 && status <= 0xBF);

    char desiredKey = 'x';
    char keyLocation = 'm';

    if (isNoteOn || isNoteOff) {
        if (data1 > 0 && data1 < 36) {
            desiredKey = lowNotes[std::abs((int)data1 - 35)];
            keyLocation = 'l';
            if (pEightyEightKey && !*pEightyEightKey) {
                if (logger) logger->AddLog("Low note %c skipped\n", desiredKey);
                return;
            }
        } else if (data1 >= 36 && data1 <= 96) {
            desiredKey = letterNoteMap[(int)data1 - 36];
            keyLocation = 'm';
        } else if (data1 > 96 && data1 < 122) {
            desiredKey = highNotes[std::abs((int)data1 - 97)];
            keyLocation = 'h';
            if (pEightyEightKey && !*pEightyEightKey) {
                if (logger) logger->AddLog("High note %c skipped\n", desiredKey);
                return;
            }
        } else {
            if (logger) logger->AddLog("Could not find key %d\n", data1);
        }
    }

    if (isControlChange) {
        if (logger) logger->AddLog("Control change: [1]: %04X [2]: %04X\n", data1, data2);
        if (data1 == 0x40) { // Sustain Pedal
            if (pSustain && !*pSustain) {
                if (logger) logger->AddLog("Skipping sustain control\n");
                return;
            }
            const int cutoff = pSustainCutoff ? *pSustainCutoff : 64;
            if (data2 >= cutoff && !sustainOn) {
                fireAndDiscard([b = backend]{ b->sendKeyDown(' '); });
                sustainOn = true;
                if (logger) logger->AddLog("Sustain down");
            } else if (data2 < cutoff && sustainOn) {
                fireAndDiscard([b = backend]{ b->sendKeyUp(' ', 'm'); });
                sustainOn = false;
                if (logger) logger->AddLog("Sustain up");
            }
            return;
        }
    }

    if (isNoteOn) {
        if (data2 == 0) {
            fireAndDiscard([b = backend, desiredKey, keyLocation]{
                b->sendKeyUp(desiredKey, keyLocation);
            });
            return;
        }

        if (pVelocity && *pVelocity) {
            const char velocityChar = findVelocity(data2);
            if (prevVelocityChar == velocityChar && logger) {
                logger->AddLog("Same velocity, skipping ");
            }
            if (logger) logger->AddLog("Velocity: %c\n", velocityChar);
            fireAndDiscard([b = backend, velocityChar]{ b->setVelocity(velocityChar); });
            prevVelocityChar = velocityChar;
        } else if (logger) {
            logger->AddLog("Skipping velocity: off\n");
        }

        if (keyLocation == 'm') {
            fireAndDiscard([b = backend, desiredKey]{ b->sendKeyUp(desiredKey, 'm'); });
            fireAndDiscard([b = backend, desiredKey]{ b->sendKeyDown(desiredKey); });
        } else {
            fireAndDiscard([b = backend, desiredKey]{ b->sendOutOfRangeKey(desiredKey); });
        }

        if (logger) logger->AddLog("Note %c, location: %c\n", desiredKey, keyLocation);
        return;
    }

    if (isNoteOff) {
        if (logger) logger->AddLog("Releasing %c\n", desiredKey);
        fireAndDiscard([b = backend, desiredKey, keyLocation]{
            b->sendKeyUp(desiredKey, keyLocation);
        });
    }
}

void NoteRouter::releaseAll() {
    std::lock_guard<std::mutex> lk(mutex);
    if (sustainOn) {
        backend->sendKeyUp(' ', 'm');
        sustainOn = false;
    }
    backend->releaseAll();
}
