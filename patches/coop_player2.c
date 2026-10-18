// Co-op: player 2 in a story room (#4, the group #19). Nothing here runs
// unless a debug switch asks for it: the hooks in bm64.us.toml call these
// functions only when the host says so (src/coop/harness.cpp, BM64_COOP_P2),
// and the patched game functions below act on player 2 only once
// coop_stage_init has made a slot player 2. docs/coop/design.md, *Player 2 in
// a story room*, has the rules and the runs behind them.
#include "coop.h"

#define COOP_P2_PAD 1
#define COOP_P2_HEARTS 2        // at a room's start and at a respawn
#define COOP_RESPAWN_FRAMES 120 // how long a downed player 2 waits
#define COOP_SETTLE_FRAMES 60   // player 1 free this long before its steps count

static s32 coop_slot = -1; // player 2's slot in this room, -1 for none
static s32 coop_joined;    // whether player 2 has been spawned in this room
static s32 coop_spawn_frames; // frames until a just spawned player 2 is placed

// Player 1's trail: the tile it is on and the one it was on before its last
// own step (along x or z; a fall onto a lower floor is not a step), so that
// player 2 can be put next to player 1 on a tile player 1 has stood on.
static s32 coop_p1_tile[3];
static s32 coop_p1_left[3];
static s32 coop_p1_moved;   // whether coop_p1_left is set
static s32 coop_p1_free;    // frames player 1 has been free of walks and scenes
static s32 coop_place_pending;
static s32 coop_freeze_tile[3]; // player 1's tile when a cutscene froze it
static s32 coop_down_frames;    // > 0 while player 2 is down, until its respawn

static s32 coop_same_tile(const s32* a, const s32* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static void coop_copy_tile(s32* to, const s32* from) {
    to[0] = from[0];
    to[1] = from[1];
    to[2] = from[2];
}

// Player 2 has been spawned in this room and plays (not out after a death).
static s32 coop_partner_active(void) {
    CoopPlayer* p2;

    if (coop_slot < 0 || !coop_joined) {
        return 0;
    }
    p2 = &g_Players[coop_slot];
    return (p2->controlType == 1 || p2->controlType == 2) && p2->levelClass != NULL;
}

// Hook at the entry of func_80245CDC, the shared player init of a stage room,
// before its spawn loop. Player 2 is not spawned by the loop: it joins at
// player 1's first own step (coop_partner_tick), so that it never stands in
// the way of player 1's entry walk (#14). This sets its stats and its pending
// placement. Slot 1 reads pad 1 (controlType 1); slot 4 has no pad buffer and
// reads its command record (controlType 2), filled by coop_feed_command.
void coop_stage_init(s32 slot) {
    CoopPlayer* p1 = &g_Players[0];
    CoopPlayer* p2 = &g_Players[slot];

    coop_place_pending = 1;
    coop_joined = 0;
    coop_spawn_frames = 0;
    coop_p1_moved = 0;
    coop_p1_free = 0;
    coop_down_frames = 0;
    if (g_LevelLoader & 0x4) {
        coop_slot = -1;
        return; // battle: the battle setup owns slots 0-3
    }
    coop_slot = slot;
    p2->controlType = 0;
    p2->levelClass = NULL; // the last room's actor is gone with its room
    p2->heartCount = COOP_P2_HEARTS;
    p2->bombCount = p1->bombCount;
    p2->firepower = p1->firepower;
    p2->bombType = 0x200;
    p2->spawnLocation[0] = p1->spawnLocation[0];
    p2->spawnLocation[1] = p1->spawnLocation[1];
    p2->spawnLocation[2] = p1->spawnLocation[2];
    recomp_printf("coop: stage init overlay=0x%02X slot=%d controlType=%d hearts=%d bombs=%d fire=%d\n",
                  D_802AC5D4 & 0xFFFF, slot, (slot == 4) ? 2 : 1, p2->heartCount, p2->bombCount, p2->firepower);
}

// Hook at the entry of func_802511EC(slot, x, y, z), which puts a slot on a
// tile (room entries and story teleports, through func_802512C0 for S), when
// slot 0 is placed. Player 2 is not put on player 1's tile at once: there it
// stands in the way of player 1's scripted entry walk, which then never ends
// (#14). It waits instead (held frozen where it stands, once it has joined),
// and coop_partner_tick places it at player 1's first own step.
void coop_place_partner(s32 slot, s32 x, s32 y, s32 z) {
    if (slot != coop_slot) {
        return;
    }
    coop_place_pending = 1;
    coop_p1_tile[0] = x;
    coop_p1_tile[1] = y;
    coop_p1_tile[2] = z;
    coop_p1_moved = 0;
    if (coop_partner_active()) {
        func_80245208(slot, 1);
    }
    recomp_printf("coop: place pending slot=%d p1 tile=%d,%d,%d\n", slot, x, y, z);
}

// The end of player 2's downed time: with a shared life left, player 2 plays
// again, with full hearts, and is placed next to player 1 by the pending
// placement. Without one it stays out until the next room's init.
static void coop_respawn_partner(void) {
    CoopPlayer* p2 = &g_Players[coop_slot];

    if (D_802AC624 <= 0) {
        recomp_printf("coop: out slot=%d lives=%d\n", coop_slot, D_802AC624);
        return;
    }
    D_802AC624--;
    p2->controlType = (coop_slot == 4) ? 2 : 1;
    p2->heartCount = COOP_P2_HEARTS;
    coop_place_pending = 1;
    func_80245208(coop_slot, 1);
    recomp_printf("coop: respawn slot=%d hearts=%d lives=%d\n", coop_slot, p2->heartCount, D_802AC624);
}

// Hook at the entry of func_8023A870(slot, cause), a death (#18). While
// player 1 plays, player 2's death does not run the story death (flags 0x92,
// player 1's death animation, the room's retry): player 2 is down instead and
// respawns after COOP_RESPAWN_FRAMES, for a shared life. Returns 1 when it
// took the death; the hook then skips func_8023A870 and returns 1 as it would,
// and the damage handler puts the slot out (controlType 3), as for a battle
// death. A death of player 1, or of player 2 while player 1 is out, runs the
// story death as before.
s32 coop_partner_death(s32 slot, s32 cause) {
    CoopPlayer* p1 = &g_Players[0];

    if (slot != coop_slot || !coop_partner_active() || coop_down_frames > 0) {
        return 0;
    }
    if (p1->controlType != 1 || p1->levelClass == NULL || p1->heartCount <= 0) {
        return 0;
    }
    coop_down_frames = COOP_RESPAWN_FRAMES;
    coop_place_pending = 0;
    recomp_printf("coop: down slot=%d cause=%d lives=%d\n", slot, cause, D_802AC624);
    return 1;
}

// Hook at the entry of func_8024E088(0), player 1's update, every frame:
// counts down a downed player 2's respawn (#18) and follows player 1's trail.
// Only player 1's own steps count, not those of a scripted walk or a scene,
// nor those in the COOP_SETTLE_FRAMES after one: a pending placement waits
// for one, then puts player 2 on the tile player 1 just left. Player 1 stood there, so it is floor, it is next to player 1,
// and it is not a tile the room closes behind an entry walk (in room 0x29 the
// tile before the walk's last is blocked afterwards, and player 2 put there
// just before took a hit of cause 3 and died).
void coop_partner_tick(s32 slot) {
    CoopPlayer* p1 = &g_Players[0];
    s32 tile[3];

    if (slot != coop_slot || p1->levelClass == NULL) {
        return;
    }
    if (coop_down_frames > 0 && --coop_down_frames == 0) {
        coop_respawn_partner();
    }
    func_802454F8(0, tile);
    if (p1->flags & (COOP_FLAG_WALK | COOP_FLAG_FROZEN)) {
        coop_p1_moved = 0; // only player 1's own steps count
        coop_p1_free = 0;
    } else if (coop_p1_free < COOP_SETTLE_FRAMES) {
        coop_p1_free++; // room 0x29 still changes the tiles behind its entry walk
    } else if (tile[0] != coop_p1_tile[0] || tile[2] != coop_p1_tile[2]) {
        coop_copy_tile(coop_p1_left, coop_p1_tile);
        coop_p1_moved = 1;
    }
    coop_copy_tile(coop_p1_tile, tile); // a fall changes only the floor level
    if (!coop_place_pending || !coop_p1_moved || (coop_joined && !coop_partner_active())) {
        return; // a downed player 2 waits for its respawn
    }
    if (!coop_joined) {
        // spawned where it will be placed (without an actor func_802511EC sets
        // the spawn point): a new actor is put back at the origin on its first
        // frames, after any placement, so it is held frozen and placed again
        // a few frames on
        func_802511EC(slot, coop_p1_left[0], coop_p1_left[1], coop_p1_left[2]);
        g_Players[slot].controlType = (slot == 4) ? 2 : 1;
        g_SpawnPlayer(slot);
        func_80245208(slot, 1);
        coop_joined = 1;
        coop_spawn_frames = 3;
        return;
    }
    if (coop_spawn_frames > 0) {
        coop_spawn_frames--;
        return;
    }
    coop_place_pending = 0;
    recomp_printf("coop: place slot=%d tile=%d,%d,%d p1 tile=%d,%d,%d\n", slot, coop_p1_left[0], coop_p1_left[1],
                  coop_p1_left[2], tile[0], tile[1], tile[2]);
    func_802511EC(slot, coop_p1_left[0], coop_p1_left[1], coop_p1_left[2]);
    func_80245208(slot, 0);
}

// Hooks at the entries of func_802452E8(on) (with the idle animation) and
// func_80245280(on), the cutscene freeze and release of S, when S is player 1:
// player 2 is frozen and released with player 1 (#16), the way func_8023C170
// freezes every player. While its placement is pending player 2 is held
// frozen already and left alone; when player 1 was moved during the scene (a
// scripted walk), the release keeps player 2 frozen until it is placed next to
// player 1 again.
void coop_freeze_partner(s32 on, s32 anim) {
    s32 tile[3];

    if (!coop_partner_active() || coop_place_pending) {
        return; // not joined yet, or held frozen for its placement already
    }
    if (!on && g_Players[0].levelClass != NULL) {
        func_802454F8(0, tile);
        if (!coop_same_tile(tile, coop_freeze_tile)) {
            coop_place_pending = 1;
            recomp_printf("coop: place pending slot=%d after a scene\n", coop_slot);
            return;
        }
    }
    if (on && !(g_Players[coop_slot].flags & COOP_FLAG_FROZEN) && g_Players[0].levelClass != NULL) {
        func_802454F8(0, coop_freeze_tile);
    }
    recomp_printf("coop: freeze partner slot=%d on=%d\n", coop_slot, on);
    if (anim) {
        D_802AC894 = coop_slot;
        func_802452E8(on);
        D_802AC894 = 0;
    } else {
        func_80245208(coop_slot, on);
    }
}

// Whether player 2 stands in a rectangle of tiles (x0..x1, z0..z1) and, if
// y1 >= y0, on a floor level y0..y1; refreshes its entry of D_802ACEC8.
static s32 coop_partner_inside(s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1) {
    s32* tile;

    if (!coop_partner_active() || coop_place_pending) {
        return 0;
    }
    tile = D_802ACEC8[coop_slot];
    func_802454F8(coop_slot, tile);
    if (tile[0] < x0 || tile[0] > x1 || tile[2] < z0 || tile[2] > z1) {
        return 0;
    }
    if (y1 >= y0 && (tile[1] < y0 || tile[1] > y1)) {
        return 0;
    }
    recomp_printf("coop: trigger slot=%d tile=%d,%d,%d\n", coop_slot, tile[0], tile[1], tile[2]);
    return 1;
}

// The three rectangle tests rooms use for exits and event triggers (#17),
// rewritten from the game's code: true when S is inside, as before, or when
// player 2 is. S's tile is read first and kept in D_802ACEC8[S], as the
// originals do, for callers that read it afterwards.

// func_802457D8(x0, z0, x1, z1): S inside the rectangle (any floor level).
RECOMP_PATCH s32 func_802457D8(s32 x0, s32 z0, s32 x1, s32 z1) {
    s32 s = D_802AC894;
    s32* tile = D_802ACEC8[s];

    func_802454F8(s, tile);
    if (tile[0] >= x0 && tile[2] >= z0 && tile[0] <= x1 && tile[2] <= z1) {
        return 1;
    }
    return s == 0 && coop_partner_inside(x0, 0, z0, x1, -1, z1);
}

// func_802456FC(x0, y, z0, x1, z1): S inside the rectangle on floor level y.
RECOMP_PATCH s32 func_802456FC(s32 x0, s32 y, s32 z0, s32 x1, s32 z1) {
    s32 s = D_802AC894;
    s32* tile = D_802ACEC8[s];

    func_802454F8(s, tile);
    if (tile[0] >= x0 && tile[2] >= z0 && tile[0] <= x1 && tile[2] <= z1 && tile[1] == y) {
        return 1;
    }
    return s == 0 && coop_partner_inside(x0, y, z0, x1, y, z1);
}

// func_802455A8(x0, y0, z0, x1, y1, z1): S inside the box of tiles.
RECOMP_PATCH s32 func_802455A8(s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1) {
    s32 s = D_802AC894;
    s32* tile = D_802ACEC8[s];

    func_802454F8(s, tile);
    if (tile[0] >= x0 && tile[2] >= z0 && tile[0] <= x1 && tile[2] <= z1 && tile[1] >= y0 && tile[1] <= y1) {
        return 1;
    }
    return s == 0 && coop_partner_inside(x0, y0, z0, x1, y1, z1);
}

// Hook at the entry of func_8024E088(slot), the player update: fills the
// co-op slot 4's command record from pad 1 with the same accessors the update
// uses for a controlType 1 slot: the stick as an angle (degrees, before the
// camera's yaw), an elevation and a length, and the held and newly pressed
// buttons. Slot 4 then moves like a pad player (#13).
void coop_feed_command(s32 slot) {
    CoopCommand* cmd = &g_ComputerPlayerMovement[slot];

    func_80239880(COOP_P2_PAD, 0, cmd->move);
    cmd->held = func_80239FF8(COOP_P2_PAD);
    cmd->pressed = func_802399AC(COOP_P2_PAD);
}

// From the hook in the item collision callback (func_80279CA8), which has the
// actor that touches an item: the slot of a player who leaves the item where
// it is, else -1 (#36). While player 2 plays, a heart (type 4) passes through
// a player it would give nothing, so a full player does not use up the
// other's knocked-out heart: the pickup (func_802797BC) sets hearts to 2, or
// adds one with stage flag 0x8 for slots 1-4.
s32 coop_item_passes(u8* actor, u8* item) {
    s32 slot;

    if (!coop_partner_active() || *(s16*)(item + 0x20) != 4) {
        return -1;
    }
    for (slot = 0; slot < 5; slot++) {
        if (g_Players[slot].levelClass == actor) {
            if ((D_802AC5C0 & 0x8) && slot != 0) {
                return -1;
            }
            return g_Players[slot].heartCount >= 2 ? slot : -1;
        }
    }
    return -1;
}
