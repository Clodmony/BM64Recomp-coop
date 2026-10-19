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

#define COOP_ENEMY_RECORDS 16
#define COOP_ENEMY_SIZE 0xA4
#define COOP_ENEMY_START 0x14

static s8 coop_enemy_logged[COOP_ENEMY_RECORDS]; // slot * 2 + ghost logged last, + 1; 0 none
static s8 coop_enemy_range_slot[COOP_ENEMY_RECORDS]; // the slot the range tests use: S, 0

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

// From the hooks in the main-code enemy AI that aim at a player (the chase,
// the line-up tests, the aimed moves), after it asked for S (s): the slot it
// aims at, S as the game chose it. Logs the choice per enemy record whenever
// it, or its ghost flag, changes.
s32 coop_enemy_aim(u8* enemy, s32 s, s32 p2) {
    f32* pos = coop_actor_pos(*(u8**)enemy);
    s32 slot = s;
    s32 ghost = coop_is_ghost(slot);
    s32 i = coop_enemy_index(enemy);

    if (i >= 0 && coop_enemy_logged[i] != slot * 2 + ghost + 1) {
        coop_enemy_logged[i] = slot * 2 + ghost + 1;
        recomp_printf("coop: enemy aim rec=%d slot=%d ghost=%d x=%d z=%d\n", i, slot, ghost, (s32)pos[0],
                      (s32)pos[2]);
    }
    return slot;
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
