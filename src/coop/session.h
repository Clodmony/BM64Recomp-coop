#ifndef COOP_SESSION_H
#define COOP_SESSION_H

#include <cstdint>
#include <optional>

// The session core of online co-op (#21; src/coop/session.cpp,
// docs/coop/design.md, *The session core*).
namespace coop {
    // The pads of this machine, read the way recomp::get_n64_input reads them
    // without a session (keyboard, controller or the input script).
    using LocalInput = bool (*)(int pad, uint16_t* buttons, float* x, float* y);

    // Whether BM64_COOP_SESSION asks for a session (host or client), whether
    // or not it is connected.
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
