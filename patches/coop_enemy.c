// Co-op: enemies and the two players (#49, #51; docs/coop/design.md,
// *Enemies and two players*). The game's enemy code knows one player, the
// story slot S. The hooks in bm64.us.toml call these functions where the
// main-code enemy code has just asked for S, only while player 2 is on
// (src/coop/harness.cpp, coop_p2_slot, passed in as p2) and only when S is
// player 1. Everything here reads game state alone (positions, flags), the
// same on both machines of an online session, so the lockstep holds.
#include "coop.h"

// The enemy records: 16 of 0xA4 bytes (US and Europe 0x800BA900, low bss).
// +0x0 the record's state (2: waiting to spawn), +0x4 its countdown; the
// enemy itself starts at +0x14 with its actor (position at +0x40, +0x10, as
// a player's), +0x84 its entry in g_EnemySlots (+0x0 the enemy type).
extern u8 D_800BA900[16 * 0xA4];
extern s16 g_EnemySlots[];
extern u8 D_800BB340[16 * 0x14]; // the enemy process table
s32 func_8026EE18(f32 y);                 // the floor level of a height
void func_8028275C(f32 x, f32 y, f32 z);  // take out the enemies within 200 units

#define COOP_ENEMY_RECORDS 16
#define COOP_ENEMY_SIZE 0xA4
#define COOP_ENEMY_START 0x14

// How coop_enemy_nearest measures and whom it counts.
#define COOP_NEAR_LIVING 1 // leave a ghost out (a target, #49)
#define COOP_NEAR_BOX 2    // the larger of |dx| and |dz|, as the enemy code's ranges (#51); else dx*dx + dz*dz
#define COOP_NEAR_FLOOR 4  // a player on the enemy's floor level first

static s8 coop_enemy_logged[COOP_ENEMY_RECORDS]; // slot * 2 + ghost logged last, + 1; 0 none
static s8 coop_enemy_range_slot[COOP_ENEMY_RECORDS]; // the slot the last range test used

// The record index of an enemy (record + 0x14), -1 for an enemy outside the
// table.
static s32 coop_enemy_index(u8* enemy) {
    s32 offset = enemy - (D_800BA900 + COOP_ENEMY_START);

    if (offset < 0 || offset >= COOP_ENEMY_RECORDS * COOP_ENEMY_SIZE || offset % COOP_ENEMY_SIZE != 0) {
        return -1;
    }
    return offset / COOP_ENEMY_SIZE;
}

static f32* coop_actor_pos(u8* actor) {
    return (f32*)(*(u8**)(actor + 0x40) + 0x10);
}

static s32 coop_enemy_type(u8* enemy) {
    return g_EnemySlots[*(s32*)(enemy + 0x84) * 2];
}

static s32 coop_is_ghost(s32 slot) {
    return (g_Players[slot].states & COOP_STATE_GHOST) != 0;
}

// A co-op player in play: an actor and a pad or a command record (player 2
// only once it has joined the room).
static s32 coop_in_play(s32 slot) {
    CoopPlayer* p = &g_Players[slot];

    return (p->controlType == 1 || p->controlType == 2) && p->levelClass != NULL;
}

static f32 coop_abs(f32 v) {
    return v < 0.0f ? -v : v;
}

// The co-op player nearest pos: player 1 (slot 0) or player 2 (slot p2), each
// while in play, measured as mode says. A tie goes to player 1. S (s) when
// neither counts, or outside a story room. Only game state is read, in a
// fixed order, so both machines of a session choose the same.
static s32 coop_enemy_nearest(s32 s, s32 p2, const f32* pos, s32 mode) {
    s32 slots[2];
    s32 best = -1;
    s32 best_away = 0;
    f32 best_d = 0.0f;
    s32 floor = 0;
    s32 i;

    if (s != 0 || p2 <= 0 || p2 >= 5 || (g_LevelLoader & 0x4)) {
        return s; // not player 1's story room: the game's own choice
    }
    if (mode & COOP_NEAR_FLOOR) {
        floor = func_8026EE18(pos[1]);
    }
    slots[0] = 0;
    slots[1] = p2;
    for (i = 0; i < 2; i++) {
        s32 slot = slots[i];
        s32 away = 0;
        f32* at;
        f32 dx, dz, d;

        if (!coop_in_play(slot) || ((mode & COOP_NEAR_LIVING) && coop_is_ghost(slot))) {
            continue;
        }
        at = coop_actor_pos(g_Players[slot].levelClass);
        dx = coop_abs(at[0] - pos[0]);
        dz = coop_abs(at[2] - pos[2]);
        if (mode & COOP_NEAR_BOX) {
            d = dx > dz ? dx : dz;
        } else {
            d = dx * dx + dz * dz;
        }
        if (mode & COOP_NEAR_FLOOR) {
            away = func_8026EE18(at[1]) != floor;
        }
        if (best < 0 || away < best_away || (away == best_away && d < best_d)) {
            best = slot;
            best_away = away;
            best_d = d;
        }
    }
    return best < 0 ? s : best;
}

// From the hooks in the main-code enemy AI that aim at a player (the chase,
// the line-up tests, the aimed moves), after it asked for S (s): the slot it
// aims at, the nearest living co-op player, one on the enemy's floor level
// first (the chase and the aimed move only act on a player on their floor).
// A ghost is never a target (owner, #22). Logs the choice per enemy record
// whenever it, or its ghost flag, changes.
s32 coop_enemy_aim(u8* enemy, s32 s, s32 p2) {
    f32* pos = coop_actor_pos(*(u8**)enemy);
    s32 slot = coop_enemy_nearest(s, p2, pos, COOP_NEAR_LIVING | COOP_NEAR_FLOOR);
    s32 ghost = coop_is_ghost(slot);
    s32 i = coop_enemy_index(enemy);

    if (i >= 0 && coop_enemy_logged[i] != slot * 2 + ghost + 1) {
        coop_enemy_logged[i] = slot * 2 + ghost + 1;
        recomp_printf("coop: enemy aim rec=%d slot=%d ghost=%d x=%d z=%d\n", i, slot, ghost, (s32)pos[0],
                      (s32)pos[2]);
    }
    return slot;
}

// From the hook in the enemy process (func_80285F8C) where it asked for S to
// test whether the enemy is still within 1300 units of S along x and z (else
// it is taken out of the room, to spawn again when S comes near): the slot to
// test, the co-op player nearest along the larger axis, so the enemy stays
// while either player is within range. A ghost counts: its machine shows the
// room around it.
s32 coop_enemy_range(u8* enemy, s32 s, s32 p2) {
    s32 slot = coop_enemy_nearest(s, p2, coop_actor_pos(*(u8**)enemy), COOP_NEAR_BOX);
    s32 i = coop_enemy_index(enemy);

    if (i >= 0) {
        coop_enemy_range_slot[i] = slot;
    }
    return slot;
}

// From the hook at the top of func_80285A9C's loop over the records, for a
// record waiting to spawn (state 2): the loop tests S's position, which it
// read before the loop (at sp + 0xA8), against the band 1050-1150 units from
// the record's enemy (spawn) and the square of 1100 units (its countdown runs
// while S is outside). Puts in the position of the co-op player nearest the
// enemy along the larger axis, so an enemy spawns when the nearer player
// comes to the band, never in front of the other, and counts down while both
// are outside.
void coop_enemy_band(u8* record, f32* tested, s32 p2) {
    u8* enemy = record + COOP_ENEMY_START;
    s32 i = coop_enemy_index(enemy);
    s32 slot;
    f32* at;

    if (D_802AC894 != 0 || i < 0) {
        return;
    }
    slot = coop_enemy_nearest(0, p2, coop_actor_pos(*(u8**)enemy), COOP_NEAR_BOX);
    if (g_Players[slot].levelClass == NULL) {
        return;
    }
    at = coop_actor_pos(g_Players[slot].levelClass);
    tested[0] = at[0];
    tested[1] = at[1];
    tested[2] = at[2];
    coop_enemy_range_slot[i] = slot;
}

// From the hook in func_80285A9C where a record waiting to spawn (state 2)
// spawns its enemy again: logged, with the player whose range it was; its
// next aim is logged anew.
void coop_enemy_spawned(u8* record) {
    u8* enemy = record + COOP_ENEMY_START;
    s32 i = coop_enemy_index(enemy);
    f32* pos = coop_actor_pos(*(u8**)enemy);

    if (i >= 0) {
        coop_enemy_logged[i] = 0;
    }
    recomp_printf("coop: enemy spawn rec=%d type=0x%X x=%d z=%d near=%d\n", i, coop_enemy_type(enemy), (s32)pos[0],
                  (s32)pos[2], i >= 0 ? coop_enemy_range_slot[i] : -1);
}

// From the hook in the enemy process (func_80285F8C) where an enemy out of
// range is taken out of the room (func_80282598): logged, with the player
// the range was tested against.
void coop_enemy_despawned(u8* enemy) {
    s32 i = coop_enemy_index(enemy);
    f32* pos = coop_actor_pos(*(u8**)enemy);

    recomp_printf("coop: enemy despawn rec=%d type=0x%X x=%d z=%d near=%d\n", i, coop_enemy_type(enemy),
                  (s32)pos[0], (s32)pos[2], i >= 0 ? coop_enemy_range_slot[i] : -1);
}

// The enemies in the room: entries of the enemy process table (16 of 0x14
// bytes at US and Europe 0x800BB340) in use (+0x4) with a record (+0x10).
static s32 coop_enemy_count(void) {
    s32 n = 0;
    s32 i;

    for (i = 0; i < COOP_ENEMY_RECORDS; i++) {
        u8* entry = D_800BB340 + i * 0x14;

        n += *(s32*)(entry + 0x4) != 0 && *(u8**)(entry + 0x10) != NULL;
    }
    return n;
}

// From the hook at the end of func_80282934, which takes out the enemies
// within 200 units of S (with a puff; rooms call it as a cutscene starts, S's
// update every 4 frames of a warp move): the same around player 2 while it
// plays and is not a ghost (a ghost takes no damage), so that neither player
// is hit as the scene ends. Logged when it took an enemy out.
void coop_enemy_clear(s32 p2) {
    f32* at;
    s32 before;

    if (D_802AC894 != 0 || p2 <= 0 || p2 >= 5 || (g_LevelLoader & 0x4) || !coop_in_play(p2) || coop_is_ghost(p2)) {
        return;
    }
    at = coop_actor_pos(g_Players[p2].levelClass);
    before = coop_enemy_count();
    func_8028275C(at[0], at[1], at[2]);
    if (coop_enemy_count() < before) {
        recomp_printf("coop: enemy clear slot=%d x=%d z=%d taken=%d\n", p2, (s32)at[0], (s32)at[2],
                      before - coop_enemy_count());
    }
}
