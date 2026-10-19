// Co-op: each player's corner of the HUD (#53; docs/coop/design.md, *The
// co-op HUD*). The story HUD (func_80242270, patched by upstream for
// widescreen) shows the clock top left and the gems top right. In a co-op
// game a second draw object, registered next to it at the scene load (the
// hook in func_8023D0F8, bm64.us.toml), draws a corner per player under them:
// player 1 left, player 2 right, on the host and the client alike. A corner
// holds the player's battle face (the "dead" face for a downed player, #22),
// its hearts, bombs and fire, and a remote icon once it has remote bombs.
// It only draws: no game state is written, so the lockstep's hash stays the
// same on both machines.
#include "coop.h"

#define COOP_HUD_Y 44          // the corners' row: the clock and gems row is 22..40
#define COOP_HUD_LEFT 28       // player 1's corner starts under the clock icon
#define COOP_HUD_RIGHT 292     // player 2's corner ends under the gem count
#define COOP_HUD_ICON 16       // icons are 16x16, digits 10x18
#define COOP_HUD_DIGIT 10
#define COOP_HUD_FACE_P1 0     // icon sheet 0x130: the battle faces, white
#define COOP_HUD_FACE_P2 2     // and black: player 2 wears slot 1's model (0x4B)
#define COOP_HUD_FACE_DEAD 8   // the same face with crossed eyes, as battle shows a dead slot
#define COOP_BOMB_RED 0x1      // player +0x18: the bomb type bits (red, remote; 0x200 normal)
#define COOP_BOMB_REMOTE 0x2

extern u8* D_802A0758; // asset 0x130, the HUD icons (faces, gems, clock, gold card)
extern u8* D_802A075C; // asset 0x131, the HUD digits
extern u8* D_802A0760; // the story HUD's draw object

void coop_hud_draw(u8* obj, Gfx** gfxP);
// A draw object (the pool at D_800A0D00): kind 1 calls draw(obj, &gfx) each
// frame; the story HUD is (func_80242270, 0x3F, 1, 0, 0) with flags 0x210.
u8* func_8022A328(void (*draw)(u8*, Gfx**), s32 priority, s32 kind, s32 arg, s32 unk);
void func_8022A1E8(u8* obj); // free a draw object

// Our own icons, as no sheet of the game has a heart, a bomb or a flame:
// 16x16, one character per pixel ('.' clear).
static const char* const coop_hud_heart_art[16] = {
    "................", "..KKKK....KKKK..", ".KRRRRK..KRRRRK.", "KRPPRRRKKRRRRRrK",
    "KRPRRRRRRRRRRRrK", "KRRRRRRRRRRRRRrK", "KRRRRRRRRRRRRRrK", ".KRRRRRRRRRRRrK.",
    "..KRRRRRRRRRrK..", "...KRRRRRRRrK...", "....KRRRRRrK....", ".....KRRRrK.....",
    "......KRrK......", ".......KK.......", "................", "................",
};
static const char* const coop_hud_bomb_art[16] = {
    "...........Y....", "..........YOY...", ".........KgY....", "........KgK.....",
    ".....KKKKgK.....", "...KKBBBBKKK....", "..KBLLBBBBBBK...", "..KBLBBBBBBBK...",
    ".KBLBBBBBBBBbK..", ".KBBBBBBBBBBbK..", ".KBBBBBBBBBbbK..", ".KBBBBBBBBBbbK..",
    "..KBBBBBBBbbK...", "..KbBBBBbbbbK...", "...KKbbbbbKK....", ".....KKKKK......",
};
static const char* const coop_hud_fire_art[16] = {
    ".......K........", "......KYK.......", "......KYK...K...", ".....KYOYK.KOK..",
    "....KYOOYK.KOK..", "...KYOOOOYKOOK..", "...KYOOROOYOOK..", "..KYOORRROOOOOK.",
    "..KYORRRRROOOOK.", "..KOORRYRRROOOK.", "..KORRYYYRRROK..", "..KORYYWYYRROK..",
    "...KORYYYYROK...", "....KKOOOOKK....", "......KKKK......", "................",
};
static const char* const coop_hud_remote_art[16] = {
    "..........KK....", ".........KRRK...", ".........KRRK...", "..........KK....",
    "..........Kg....", "..........Kg....", "......KKKKKKKK..", "......KGGGGGGK..",
    "......KGRRWGGK..", "......KGRRGGGK..", "......KGGGGGGK..", "......KGgGgGgK..",
    "......KGGGGGGK..", "......KGgGgGgK..", "......KGGGGGGK..", "......KKKKKKKK..",
};

typedef enum CoopHudIcon {
    COOP_ICON_HEART,
    COOP_ICON_BOMB,
    COOP_ICON_RED_BOMB,
    COOP_ICON_FIRE,
    COOP_ICON_REMOTE,
    COOP_ICON_COUNT
} CoopHudIcon;

static u16 coop_hud_tex[COOP_ICON_COUNT][16 * 16] __attribute__((aligned(8))); // RGBA 5551
static s32 coop_hud_painted;

// What a corner shows, kept to log it when it changes.
typedef struct CoopHudCorner {
    s32 shown;
    s32 x;
    s32 face;
    s32 hearts;
    s32 bombs;
    s32 fire;
    s32 type;
} CoopHudCorner;

static u8* coop_hud_obj;  // our draw object while a co-op stage room runs
static s32 coop_hud_p2;   // player 2's slot
static CoopHudCorner coop_hud_last[2];

static u16 coop_hud_rgba(s32 r, s32 g, s32 b) {
    return ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1;
}

static u16 coop_hud_color(char c, s32 red) {
    if (red) { // the red bomb: the body red instead of blue
        c = (c == 'B') ? 'R' : (c == 'b') ? 'r' : (c == 'L') ? 'P' : c;
    }
    switch (c) {
        case 'K': return coop_hud_rgba(24, 16, 24);
        case 'W': return coop_hud_rgba(248, 248, 248);
        case 'R': return coop_hud_rgba(232, 48, 40);
        case 'r': return coop_hud_rgba(152, 24, 32);
        case 'P': return coop_hud_rgba(248, 152, 152);
        case 'B': return coop_hud_rgba(48, 56, 120);
        case 'b': return coop_hud_rgba(24, 28, 64);
        case 'L': return coop_hud_rgba(112, 128, 200);
        case 'Y': return coop_hud_rgba(255, 232, 64);
        case 'O': return coop_hud_rgba(248, 136, 24);
        case 'G': return coop_hud_rgba(168, 168, 168);
        case 'g': return coop_hud_rgba(96, 96, 96);
        default: return 0;
    }
}

static void coop_hud_paint(CoopHudIcon icon, const char* const* art, s32 red) {
    s32 x;
    s32 y;

    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            coop_hud_tex[icon][y * 16 + x] = coop_hud_color(art[y][x], red);
        }
    }
}

// An image of a HUD sheet (0x130, 0x131): entry n at +0xC + 12 * n, its
// offset in the sheet at +8; an image is entry n, its palette entry n + 1.
static u32 coop_hud_image(u8* sheet, s32 n) {
    return (u32)sheet + *(u32*)(sheet + 0x14 + 12 * n);
}

static Gfx* coop_hud_rect(Gfx* gfx, s32 x, s32 y, s32 w, s32 h) {
    gSPTextureRectangle(gfx++, x << 2, y << 2, (x + w) << 2, (y + h) << 2, 0, 0, 0, 0x0400, 0x0400);
    return gfx;
}

// A face of icon sheet 0x130, as the battle HUD draws one (func_80241BB0).
static Gfx* coop_hud_face(Gfx* gfx, s32 face, s32 x, s32 y) {
    gDPSetTextureLUT(gfx++, G_TT_RGBA16);
    gDPLoadTLUT_pal256(gfx++, coop_hud_image(D_802A0758, face + 1));
    gDPLoadTextureBlock_4b(gfx++, coop_hud_image(D_802A0758, face), G_IM_FMT_CI, 16, 16, 0,
                           G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                           G_TX_NOLOD);
    return coop_hud_rect(gfx, x, y, COOP_HUD_ICON, COOP_HUD_ICON);
}

static Gfx* coop_hud_icon(Gfx* gfx, CoopHudIcon icon, s32 x, s32 y) {
    gDPSetTextureLUT(gfx++, G_TT_NONE);
    gDPLoadTextureBlock(gfx++, coop_hud_tex[icon], G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD);
    return coop_hud_rect(gfx, x, y, COOP_HUD_ICON, COOP_HUD_ICON);
}

// The digits of sheet 0x131 in the clock's palette, as the story HUD draws
// them (func_80242270): digit d is image 2 * d.
static Gfx* coop_hud_digit(Gfx* gfx, s32 d, s32 x, s32 y) {
    gDPSetTextureLUT(gfx++, G_TT_RGBA16);
    gDPLoadTLUT_pal16(gfx++, 0, coop_hud_image(D_802A075C, 1));
    gDPLoadTextureTile_4b(gfx++, coop_hud_image(D_802A075C, 2 * d), G_IM_FMT_CI, 10, 18, 0, 0, 9, 17, 0,
                          G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                          G_TX_NOLOD);
    return coop_hud_rect(gfx, x, y, COOP_HUD_DIGIT, 18);
}

static s32 coop_hud_clamp(s32 v) {
    return v < 0 ? 0 : (v > 99 ? 99 : v);
}

static s32 coop_hud_digits(s32 v) {
    return v >= 10 ? 2 : 1;
}

// An icon followed by a number: returns the x after it.
static Gfx* coop_hud_count(Gfx* gfx, CoopHudIcon icon, s32 v, s32* x) {
    gfx = coop_hud_icon(gfx, icon, *x, COOP_HUD_Y + 1);
    *x += COOP_HUD_ICON;
    if (v >= 10) {
        gfx = coop_hud_digit(gfx, v / 10, *x, COOP_HUD_Y);
        *x += COOP_HUD_DIGIT;
    }
    gfx = coop_hud_digit(gfx, v % 10, *x, COOP_HUD_Y);
    *x += COOP_HUD_DIGIT + 4;
    return gfx;
}

// Corner i (0 player 1, top left; 1 player 2, top right): what it shows,
// logged when it changes.
static void coop_hud_look(s32 i, CoopHudCorner* c) {
    s32 slot = (i == 0) ? 0 : coop_hud_p2;
    CoopPlayer* p = &g_Players[slot];
    CoopHudCorner* last = &coop_hud_last[i];
    s32 width;

    c->shown = p->controlType != 0;
    c->face = (i == 0) ? COOP_HUD_FACE_P1 : COOP_HUD_FACE_P2;
    if (p->controlType == 3 || (p->states & COOP_STATE_GHOST)) {
        c->face += COOP_HUD_FACE_DEAD; // out (battle) or down (#22), as func_80241BB0 tells them
    }
    c->hearts = coop_hud_clamp(p->heartCount);
    c->bombs = coop_hud_clamp(p->bombCount);
    c->fire = coop_hud_clamp(p->firepower);
    c->type = p->bombType & 0xFFFF;
    width = COOP_HUD_ICON + 2 + 3 * (COOP_HUD_ICON + 4) - 4 +
            COOP_HUD_DIGIT * (coop_hud_digits(c->hearts) + coop_hud_digits(c->bombs) + coop_hud_digits(c->fire));
    if (c->type & COOP_BOMB_REMOTE) {
        width += 4 + COOP_HUD_ICON;
    }
    c->x = (i == 0) ? COOP_HUD_LEFT : COOP_HUD_RIGHT - width;
    if (c->shown != last->shown || (c->shown && (c->x != last->x || c->face != last->face ||
                                                 c->hearts != last->hearts || c->bombs != last->bombs ||
                                                 c->fire != last->fire || c->type != last->type))) {
        if (c->shown) {
            recomp_printf("coop: hud slot=%d corner=%s x=%d y=%d face=%d hearts=%d bombs=%d fire=%d type=0x%X\n", slot,
                          (i == 0) ? "left" : "right", c->x, COOP_HUD_Y, c->face, c->hearts, c->bombs, c->fire,
                          c->type);
        } else {
            recomp_printf("coop: hud slot=%d hidden\n", slot);
        }
        *last = *c;
    }
}

static Gfx* coop_hud_corner(Gfx* gfx, s32 i) {
    CoopHudCorner c;
    s32 x;

    coop_hud_look(i, &c);
    if (!c.shown) {
        return gfx;
    }
    // anchored to its side of the screen as upstream's widescreen HUD anchors
    // the clock and the gems (func_80242270)
    gEXPushScissor(gfx++);
    gEXSetScissor(gfx++, G_SC_NON_INTERLACE, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 240);
    if (i == 0) {
        gEXSetRectAlign(gfx++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, 0, 0, 0, 0);
    } else {
        gEXSetRectAlign(gfx++, G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT, -(320) * 4, 0, -(320) * 4, 0);
    }
    x = c.x;
    gfx = coop_hud_face(gfx, c.face, x, COOP_HUD_Y + 1);
    x += COOP_HUD_ICON + 2;
    gfx = coop_hud_count(gfx, COOP_ICON_HEART, c.hearts, &x);
    gfx = coop_hud_count(gfx, (c.type & COOP_BOMB_RED) ? COOP_ICON_RED_BOMB : COOP_ICON_BOMB, c.bombs, &x);
    gfx = coop_hud_count(gfx, COOP_ICON_FIRE, c.fire, &x);
    if (c.type & COOP_BOMB_REMOTE) {
        gfx = coop_hud_icon(gfx, COOP_ICON_REMOTE, x, COOP_HUD_Y + 1);
    }
    gEXSetRectAlign(gfx++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    gEXPopScissor(gfx++);
    return gfx;
}

// The draw object's callback, once a frame while the room runs.
void coop_hud_draw(u8* obj, Gfx** gfxP) {
    Gfx* gfx = *gfxP;
    s32 i;

    // shown when the story HUD is (its flag 0x1 hides a draw object)
    if (D_802A0758 == NULL || D_802A075C == NULL || D_802A0760 == NULL || (*(u32*)(D_802A0760 + 0xC) & 1)) {
        return;
    }
    gDPSetRenderMode(gfx++, G_RM_AA_TEX_EDGE, G_RM_AA_TEX_EDGE2);
    gDPSetCombineMode(gfx++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gSPTexture(gfx++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTexturePersp(gfx++, G_TP_NONE);
    for (i = 0; i < 2; i++) {
        gfx = coop_hud_corner(gfx, i);
    }
    gDPSetTexturePersp(gfx++, G_TP_PERSP);
    gDPSetTextureLUT(gfx++, G_TT_NONE);
    *gfxP = gfx;
}

// From the hook in the scene load (func_8023D0F8), right after it registered
// the story HUD, in a co-op game (player 2 in p2_slot): our own draw object
// next to it, with the same priority and flags.
void coop_hud_register(s32 p2_slot) {
    if (!coop_hud_painted) {
        coop_hud_paint(COOP_ICON_HEART, coop_hud_heart_art, 0);
        coop_hud_paint(COOP_ICON_BOMB, coop_hud_bomb_art, 0);
        coop_hud_paint(COOP_ICON_RED_BOMB, coop_hud_bomb_art, 1);
        coop_hud_paint(COOP_ICON_FIRE, coop_hud_fire_art, 0);
        coop_hud_paint(COOP_ICON_REMOTE, coop_hud_remote_art, 0);
        coop_hud_painted = 1;
    }
    // a teardown frees it (coop_hud_free); should one have been skipped, drop
    // the old object while it is still ours
    if (coop_hud_obj != NULL && *(u32*)(coop_hud_obj + 0x3C) == (u32)coop_hud_draw) {
        func_8022A1E8(coop_hud_obj);
    }
    coop_hud_p2 = p2_slot;
    coop_hud_last[0].shown = coop_hud_last[1].shown = -1;
    coop_hud_obj = func_8022A328(coop_hud_draw, 0x3F, 1, 0, 0);
    if (coop_hud_obj == NULL) {
        recomp_printf("coop: hud none: no free draw object\n");
        return;
    }
    *(u32*)(coop_hud_obj + 0xC) |= 0x210;
    recomp_printf("coop: hud on p2=%d\n", p2_slot);
}

// From the hook in the stage teardown (func_8023D8A0), where it frees the
// story HUD.
void coop_hud_free(void) {
    if (coop_hud_obj == NULL) {
        return;
    }
    func_8022A1E8(coop_hud_obj);
    coop_hud_obj = NULL;
    recomp_printf("coop: hud off\n");
}
