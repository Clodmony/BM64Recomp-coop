#ifndef COOP_DEBUG_H
#define COOP_DEBUG_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace coop {
    // With BM64_COOP_AUTOSTART=1, does once what the launcher's Start button
    // does: starts the game from its stored ROM (<game_id>.z64 in the app
    // folder) and hides the launcher. tools/run.sh sets it. Called on the UI
    // thread when the launcher is shown, so the runtime is up by then. With
    // BM64_COOP_LAUNCHER set, its Co-op menu's Host or Join instead
    // (src/coop/session_ui.cpp).
    void autostart();

    // Audio the game handed the host (frames), the host queue's depth when it
    // came and whether the host skipped samples for a queue over 100 ms;
    // reported by the frame log (BM64_COOP_LOG_FRAMES).
    void note_audio(size_t frames, uint64_t queued_us, bool skipped);

    // A playtest's log (#29): on Windows, a crash writes its exception and
    // stack to the log, and without a console stdout and stderr go to
    // coop.log in the app folder, the run before's kept as coop.prev.log
    // (the Linux kit's start.sh does the same). Called once at startup.
    void start_log();

    // The last `limit` bytes of the run before's log (coop.prev.log in the
    // app folder), empty without one; a session sends it to the other side.
    std::string previous_log(size_t limit);
}

#endif
