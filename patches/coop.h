// Co-op: the game state and functions the co-op patches use
// (patches/coop_player2.c, patches/coop_harness.c). Addresses are the US
// ones (docs/coop/design.md, *Addresses*); the Europe build maps them.
#ifndef COOP_H
#define COOP_H

#include "patches.h"

// g_Players[5], stride 0xC8 (docs/coop/design.md, sections 1-4). Only the
// fields the co-op code touches are named.
typedef struct CoopPlayer {
    /* 0x00 */ f32 spawnLocation[3];
    /* 0x0C */ f32 yaw;
    /* 0x10 */ s32 flags;       // 0x400 scripted walk, 0x2010000 cutscene freeze
    /* 0x14 */ s32 heartCount;
    /* 0x18 */ s32 bombType;
    /* 0x1C */ s32 bombCount;
    /* 0x20 */ s32 firepower;
    /* 0x24 */ s32 controlType; // 0 none, 1 pad = slot, 2 command record, 3 out
    /* 0x28 */ u8* levelClass;  // +0x40: the model object, position at +0x10
    /* 0x2C */ u8 pad2C[0x68 - 0x2C];
    /* 0x68 */ s32 states;      // func_80243D90 sets, func_80243EB4 tests; 0x200 battle ghost
    /* 0x6C */ u8 pad6C[0xC8 - 0x6C];
} CoopPlayer;

// The command record a controlType 2 slot reads in func_8024E088.
typedef struct CoopCommand {
    /* 0x0 */ f32 move[3];
    /* 0xC */ u16 held;
    /* 0xE */ u16 pressed;
} CoopCommand;

#define COOP_FLAG_WALK 0x400
#define COOP_FLAG_FROZEN 0x2010000 // control lock and damage guard
#define COOP_STATE_GHOST 0x200     // the battle ghost (func_80243E90 sets it, #22)

extern CoopPlayer g_Players[5];
extern CoopCommand g_ComputerPlayerMovement[5];
extern s32 g_LevelLoader;   // flow flags; 0x4 battle
extern s32 D_802AC5D4;      // overlay ID being played
extern s32 D_802AC5E8;      // world (0-5) and stage (0-3) of the room being played
extern s32 D_802AC5EC;
extern s32 D_802AC894;      // S, the story player's slot
extern s32 D_802AC624;      // lives (story session block +0x14)
extern s32 D_802AC5C0;      // stage flags; 0x8: a heart adds one for slots 1-4 (func_802797BC)
extern s32 D_802ACEC8[5][3]; // each slot's tile, as the rectangle tests last read it
extern u8 D_80063000[0x1B8]; // the EEPROM mirror: a header block, then files 0-2 of 0x90 bytes
// The camera (docs/coop/design.md, *The camera*): the eye, and its yaw and
// pitch in degrees and its distance from the point it looks at (the player
// update adds the yaw to the stick's angle).
extern f32 D_8008F3E8[3];
extern f32 D_8008F3F8[3];

void func_8023DA04(s32 continued);
void func_8023AE6C(s32 overlay, s32 entry, s32 fade);
void func_80293EA0(s32 file); // make file 0-2 the current one
s32 func_80293E1C(void);      // load the current file into the live progress block
void func_802947AC(void);     // save the live block into the current file and the EEPROM
void func_802511EC(s32 slot, s32 x, s32 y, s32 z);
void func_802454F8(s32 slot, s32* tile);
void g_SpawnPlayer(s32 slot);
void func_80245208(s32 slot, s32 on);
void func_8022979C(u8* obj, s32 part, s32 variant); // a model part's variant
void func_80231408(u8* actor, s32 bits);            // actor +0x8 bits on (draw mode 0x80000)
void func_802313DC(u8* actor, s32 bits);            // and off
void func_8027B39C(s32 type, s32 slot);             // knock an item out of a slot (4 a heart)
void func_802790E0(u8* item);                       // remove an item
void func_802452E8(s32 on);
void func_80239880(s32 pad, s32 unk, f32* out);
s32 func_80239FF8(s32 pad);
s32 func_802399AC(s32 pad);
s32 func_80239714(s32 pad, s32 back);
s32 func_802395A8(s32 pad, s32 back);

#endif
