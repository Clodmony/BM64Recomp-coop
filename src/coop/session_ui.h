#ifndef COOP_SESSION_UI_H
#define COOP_SESSION_UI_H

// The launcher's Co-op menu and the session's state on screen (#28,
// src/coop/session_ui.cpp). All of it runs on the UI thread.
namespace coop {
    // Shows the Co-op menu: host on a port, or join an address and port. The
    // launcher's Co-op button calls it.
    void open_session_menu();

    // Shows a change of the session's state (waiting, connected, refused,
    // lost) as a prompt; called on every UI frame.
    void update_session_ui();

    // With BM64_COOP_LAUNCHER=host:<port> or join:<host>:<port>, fills the
    // Co-op menu with it and presses Host or Join; with BM64_COOP_SESSION,
    // connects and then starts the game. True if it did either. Called once by
    // coop::autostart().
    bool launcher_autostart();
}

#endif
