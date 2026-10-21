// Co-op: the game's side of an online session (#21; src/coop/session.cpp,
// docs/coop/design.md, *The session core*). Both machines run the same frames
// on the same pads; a hash of the state that tells players apart is compared
// every frame to find a desync.
#include "coop.h"

// g_initRandom's state: its index (g_nextRandomRaw counts it 1-55), a word and
// the 55-entry table (g_RandomTable). US 0x802ACFE0, Europe 0x802ACFD0: until
// #75 the hash read 0x10 bytes early (the Europe address taken as the US one,
// design.md, *Addresses*) and so missed the table's last word.
extern u32 g_RandomIndex[2 + 55];
// The save (docs/coop/design.md, section 11): besides the EEPROM mirror
// (coop.h), the live progress block with the stage-local flags
// (0x8008E570..0x8008E647) and the current file's index.
extern u32 D_8008E570[0xD8 / 4];
extern s32 D_802A1DB4;

static u32 coop_hash_word(u32 hash, u32 word) {
    s32 i;

    for (i = 0; i < 4; i++) {
        hash ^= (word >> (i * 8)) & 0xFF;
        hash *= 16777619; // FNV-1a
    }
    return hash;
}

static u32 coop_hash_words(u32 hash, const u32* words, s32 count) {
    s32 i;

    for (i = 0; i < count; i++) {
        hash = coop_hash_word(hash, words[i]);
    }
    return hash;
}

// The enemy records and the enemy process table (patches/coop_enemy.c).
extern u8 D_800BA900[16 * 0xA4];
extern s16 g_EnemySlots[];
extern u8 D_800BB340[16 * 0x14];

#define COOP_HASH_PARTS 5
#define COOP_ENEMY_RECORDS 16
#define COOP_ENEMY_SIZE 0xA4

// What coop_state_hash leaves for the session, word by word in this order
// (src/coop/session.cpp, coop_session_frame, reads it so): the hash, its
// parts, and the values a desync is logged with (#75).
typedef struct CoopStateWords {
    u32 hash;                   // FNV-1a over the parts
    u32 parts[COOP_HASH_PARTS]; // scene, random, players, enemies, save
    s32 scene[4];               // overlay, flow flags, lives, S
    u32 random[2];              // the generator's index and the word after it
    struct {
        s32 controlType;
        s32 states;
        s32 flags;
        f32 pos[3];             // 0 without an actor
    } players[5];
    struct {
        s32 state;              // the record's +0x0
        s32 type;               // -1: not in the room
        f32 pos[3];
    } enemies[COOP_ENEMY_RECORDS];
} CoopStateWords;

static CoopStateWords coop_state;

// The enemy record a process table entry runs, -1 for an entry not in use.
static s32 coop_entry_record(s32 entry) {
    u8* e = D_800BB340 + entry * 0x14;
    s32 offset;

    if (*(s32*)(e + 0x4) == 0 || *(u8**)(e + 0x10) == NULL) {
        return -1;
    }
    offset = *(u8**)(e + 0x10) - D_800BA900;
    if (offset < 0 || offset >= COOP_ENEMY_RECORDS * COOP_ENEMY_SIZE || offset % COOP_ENEMY_SIZE != 0) {
        return -1;
    }
    return offset / COOP_ENEMY_SIZE;
}

// The enemies: each record as it is (16 of 0xA4 bytes), then the actor
// position of each one in the room (one the process table runs).
static u32 coop_hash_enemies(u32 hash) {
    s32 i;

    hash = coop_hash_words(hash, (const u32*)D_800BA900, COOP_ENEMY_RECORDS * COOP_ENEMY_SIZE / 4);
    for (i = 0; i < COOP_ENEMY_RECORDS; i++) {
        coop_state.enemies[i].state = *(s32*)(D_800BA900 + i * COOP_ENEMY_SIZE);
        coop_state.enemies[i].type = -1;
        coop_state.enemies[i].pos[0] = coop_state.enemies[i].pos[1] = coop_state.enemies[i].pos[2] = 0.0f;
    }
    for (i = 0; i < COOP_ENEMY_RECORDS; i++) {
        s32 rec = coop_entry_record(i);
        u8* enemy;
        u8* actor;
        f32* pos;

        if (rec < 0) {
            continue;
        }
        enemy = D_800BA900 + rec * COOP_ENEMY_SIZE + 0x14;
        actor = *(u8**)enemy;
        if (actor == NULL || *(u8**)(actor + 0x40) == NULL) {
            continue;
        }
        pos = (f32*)(*(u8**)(actor + 0x40) + 0x10);
        coop_state.enemies[rec].type = g_EnemySlots[*(s32*)(enemy + 0x84) * 2];
        coop_state.enemies[rec].pos[0] = pos[0];
        coop_state.enemies[rec].pos[1] = pos[1];
        coop_state.enemies[rec].pos[2] = pos[2];
        hash = coop_hash_word(hash, rec);
        hash = coop_hash_words(hash, (const u32*)pos, 3);
    }
    return hash;
}

// From the per-frame hook: a hash of the state after this frame, in five
// parts. The scene (overlay, flow flags, lives, S), the random generator,
// the players (their records, command records and actor positions), the
// enemies (their records and actor positions, #75) and the save data: a
// different input, a different random number or another save shows in it
// within a frame or two, an enemy that moves another way at once. Returns
// coop_state: the session sends the parts with the hash, so a desync names
// the part that differs, and logs the values with it.
CoopStateWords* coop_state_hash(void) {
    u32 basis = 2166136261u;
    u32 hash;
    s32 slot;
    s32 i;

    coop_state.scene[0] = D_802AC5D4;
    coop_state.scene[1] = g_LevelLoader;
    coop_state.scene[2] = D_802AC624;
    coop_state.scene[3] = D_802AC894;
    coop_state.parts[0] = coop_hash_words(basis, (const u32*)coop_state.scene, 4);

    coop_state.random[0] = g_RandomIndex[0];
    coop_state.random[1] = g_RandomIndex[1];
    coop_state.parts[1] = coop_hash_words(basis, g_RandomIndex, 2 + 55);

    hash = coop_hash_words(basis, (const u32*)g_Players, sizeof(g_Players) / 4);
    hash = coop_hash_words(hash, (const u32*)g_ComputerPlayerMovement, sizeof(g_ComputerPlayerMovement) / 4);
    for (slot = 0; slot < 5; slot++) {
        u8* actor = g_Players[slot].levelClass;

        coop_state.players[slot].controlType = g_Players[slot].controlType;
        coop_state.players[slot].states = g_Players[slot].states;
        coop_state.players[slot].flags = g_Players[slot].flags;
        coop_state.players[slot].pos[0] = coop_state.players[slot].pos[1] = coop_state.players[slot].pos[2] = 0.0f;
        if (actor != NULL && g_Players[slot].controlType != 0) {
            f32* pos = (f32*)(*(u8**)(actor + 0x40) + 0x10);

            coop_state.players[slot].pos[0] = pos[0];
            coop_state.players[slot].pos[1] = pos[1];
            coop_state.players[slot].pos[2] = pos[2];
            hash = coop_hash_words(hash, (const u32*)pos, 3);
        }
    }
    coop_state.parts[2] = hash;

    coop_state.parts[3] = coop_hash_enemies(basis);

    hash = coop_hash_words(basis, (const u32*)D_80063000, 0x1B8 / 4);
    hash = coop_hash_words(hash, D_8008E570, 0xD8 / 4);
    coop_state.parts[4] = coop_hash_word(hash, D_802A1DB4);

    hash = basis;
    for (i = 0; i < COOP_HASH_PARTS; i++) {
        hash = coop_hash_word(hash, coop_state.parts[i]);
    }
    coop_state.hash = hash;
    return &coop_state;
}
