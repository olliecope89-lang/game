/*
 * FLOPPY FIGHTERS - portable game core.
 *
 * Pure C, no platform calls. The PS3 front-end (ps3_main.c) and the desktop
 * test harness (desktop/test.c) both drive this same code:
 *
 *     game_init(seed);
 *     loop { game_update(&input, dt); game_render(framebuffer, w, h, pitch); }
 *
 * Pixels are written as 0x00RRGGBB (what the PS3 XRGB framebuffer expects).
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#define GW 1280 /* logical playfield size; renderer scales to the real buffer */
#define GH 720

enum {
    BTN_PUNCH = 1, /* Square   */
    BTN_KICK  = 2, /* Triangle */
    BTN_JUMP  = 4, /* Cross    */
    BTN_FLOP  = 8, /* Circle   */
    BTN_START = 16 /* Start    */
};

typedef struct {
    float    move; /* -1 (left) .. +1 (right), deadzone already applied */
    unsigned held; /* BTN_* bits held this frame */
} Input;

void game_init(uint32_t seed);
void game_update(const Input *in, float dt);
void game_render(uint32_t *fb, int w, int h, int pitch_px);

/* ---- test / debug helpers (used by desktop/test.c) ---- */
enum { ST_TITLE = 0, ST_PLAY = 1, ST_OVER = 2 };

typedef struct {
    int   state, wave, score, kills, player_hp, enemies;
    int   nearest_valid;
    float nearest_dx;   /* signed x distance player -> nearest live enemy */
    float player_x;
    int   bad_points;   /* NaN / wildly out-of-arena points, should stay 0 */
    float max_stretch;  /* worst bone length / rest length, should stay ~1 */
} GameStats;

void game_stats(GameStats *s);
void game_draw_text_test(uint32_t *fb, int w, int h, int pitch_px);

#endif
