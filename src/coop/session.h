#ifndef COOP_SESSION_H
#define COOP_SESSION_H

#include <cstdint>
#include <optional>
#include <string>

// The session core of online co-op (#21; src/coop/session.cpp,
// docs/coop/design.md, *The session core*).
namespace coop {
    enum class SessionState { Off, Waiting, Connected, Refused, Lost, Cancelled };

    // What the session is doing, for the launcher's prompts (#28,
    // src/coop/session_ui.cpp). `text` says it in a few words: whom it waits
    // for, why it was refused or lost. `serial` grows on every change.
    struct SessionStatus {
        uint32_t serial = 0;
        SessionState state = SessionState::Off;
        bool host = false;
        std::string text;
    };

    // The latest status; any thread.
    SessionStatus session_status();

    // Ends a wait for the other side that is still going on; the game then
    // plays alone. Does nothing once the session connected or ended.
    void cancel_session();

    // From the launcher's Co-op menu, before the game starts: `spec` as in
    // BM64_COOP_SESSION (host:<port> or join:<host>:<port>), in its place. The
    // wait for the other side then has no time limit (cancel_session ends
    // it). False, with `error` filled, when spec is not one.
    bool configure_session(const std::string& spec, std::string& error);

    // Connects to the other side and exchanges the hellos on a thread of its
    // own, so the launcher can show the wait (the game, once started, draws
    // nothing until its first frame). The status then leaves Waiting; the
    // game's first pad read sends the save and starts the lockstep. A session
    // the launcher started and that did not connect is cleared (its Start then
    // plays alone); one from BM64_COOP_SESSION plays alone. Without this call
    // the game's first pad read connects.
    void connect_session();

    // The pads of this machine, read the way recomp::get_n64_input reads them
    // without a session (keyboard, controller or the input script).
    using LocalInput = bool (*)(int pad, uint16_t* buttons, float* x, float* y);

    // Whether BM64_COOP_SESSION or the launcher asks for a session (host or
    // client), whether or not it is connected.
    bool session_configured();

    // Whether the session connected at the game's start (it may have been
    // lost since).
    bool session_connected();

    // Whether this machine is the client of a session that is still live.
    bool session_live_client();

    // The pad state the game reads for pad `pad` in a session: pad 0 is the
    // host's pad, pad 1 the client's, both delayed by the same frames on both
    // machines; pads 2 and 3 are not connected. The game's first read makes
    // the connection; the per-frame hook steps the lockstep. Returns nothing
    // without a session or once it has ended (read the local pads then),
    // otherwise whether the pad is connected.
    std::optional<bool> session_input(int pad, uint16_t* buttons, float* x, float* y, LocalInput local);

    // Whether pad `pad` is connected in a session; nothing without one.
    std::optional<bool> session_pad_connected(int pad);
}

#endif
