// Co-op: the game's side of an online session (#21; src/coop/session.cpp,
// docs/coop/design.md, *The session core*). Both machines run the same frames
// on the same pads; a hash of the state that tells players apart is compared
// every frame to find a desync.
#include "coop.h"

// g_initRandom's state: the index words, the 55-entry table and what follows
// it, 0xF0 bytes from D_802ACFD0 (read from the disassembly).
extern u32 D_802ACFD0[0x3C];

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

// From the per-frame hook: a hash of the state after this frame. The players'
// records and actor positions, the random generator, the scene and the story
// session's lives: a different input or a different random number shows in
// it within a frame or two.
u32 coop_state_hash(void) {
    u32 hash = 2166136261u;
    s32 slot;

    hash = coop_hash_word(hash, D_802AC5D4);
    hash = coop_hash_word(hash, g_LevelLoader);
    hash = coop_hash_word(hash, D_802AC624);
    hash = coop_hash_word(hash, D_802AC894);
    hash = coop_hash_words(hash, D_802ACFD0, 0x3C);
    hash = coop_hash_words(hash, (const u32*)g_Players, sizeof(g_Players) / 4);
    hash = coop_hash_words(hash, (const u32*)g_ComputerPlayerMovement, sizeof(g_ComputerPlayerMovement) / 4);
    for (slot = 0; slot < 5; slot++) {
        u8* actor = g_Players[slot].levelClass;

        if (actor != NULL && g_Players[slot].controlType != 0) {
            hash = coop_hash_words(hash, (const u32*)(*(u8**)(actor + 0x40) + 0x10), 3);
        }
    }
    return hash;
}
