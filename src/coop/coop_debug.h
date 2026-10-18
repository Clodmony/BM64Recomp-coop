#ifndef COOP_DEBUG_H
#define COOP_DEBUG_H

#include <cstddef>
#include <cstdint>

namespace coop {
    // With BM64_COOP_AUTOSTART=1, does once what the launcher's Start button
    // does: starts the game from its stored ROM (<game_id>.z64 in the app
    // folder) and hides the launcher. tools/run.sh sets it. Called on the UI
    // thread when the launcher is shown, so the runtime is up by then.
    void autostart();

    // Audio the game handed the host (frames), the host queue's depth when it
    // came and whether the host skipped samples for a queue over 100 ms;
    // reported by the frame log (BM64_COOP_LOG_FRAMES).
    void note_audio(size_t frames, uint64_t queued_us, bool skipped);
}

#endif
