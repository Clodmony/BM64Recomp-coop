// Co-op: which player the camera follows (#27; docs/coop/design.md, section 5
// and *The camera*). Online, each machine draws its own view: the client's
// camera follows its own player, player 2. The choice is the machine's own
// (src/coop/harness.cpp, BM64_COOP_CAMERA), not game state, so the hook in
// the camera process (bm64.us.toml) calls this only when the host asks.
#include "coop.h"

static s32 coop_camera_last = -1; // the slot followed last, for the log

// From the hook in the camera process (func_80235D7C), where it has the story
// slot S to follow: the slot to follow instead, `wanted` while that player is
// in play (an actor and a pad or a command record), S otherwise.
s32 coop_camera_target(s32 s, s32 wanted) {
    s32 slot = s;

    if (wanted >= 0 && wanted < 5 && g_Players[wanted].levelClass != NULL &&
        (g_Players[wanted].controlType == 1 || g_Players[wanted].controlType == 2)) {
        slot = wanted;
    }
    if (slot != coop_camera_last) {
        recomp_printf("coop: camera follows slot=%d\n", slot);
        coop_camera_last = slot;
    }
    return slot;
}

// From the per-frame hook: where the camera's yaw and pitch are (D_8008F3F8,
// degrees), which a session sends with this machine's pad (#33; the player
// update turns each player's stick by its own machine's).
f32* coop_camera_angles(void) {
    return D_8008F3F8;
}
