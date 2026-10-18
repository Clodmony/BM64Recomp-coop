#ifndef COOP_DEBUG_H
#define COOP_DEBUG_H

namespace coop {
    // With BM64_COOP_AUTOSTART=1, does once what the launcher's Start button
    // does: starts the game from its stored ROM (<game_id>.z64 in the app
    // folder) and hides the launcher. tools/run.sh sets it. Called on the UI
    // thread when the launcher is shown, so the runtime is up by then.
    void autostart();
}

#endif
