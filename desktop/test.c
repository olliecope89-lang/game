/*
 * Desktop test harness for the portable game core.
 *
 *   ./test bot   <seconds> <seed> [dump_prefix]   scripted "player" fights
 *   ./test fuzz  <seconds> <seed>                 random button mashing
 *   ./test idle  <seconds> <seed> [dump_prefix]   no input at all
 *   ./test text  <file.ppm>                       font / primitive sanity image
 *
 * Frames are dumped as PPM (converted to PNG by the caller) at 1280x720.
 */
#include "../source/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define W 1280
#define H 720
static uint32_t fb[W * H];

static void dump(const char *prefix, const char *tag)
{
    char name[256];
    snprintf(name, sizeof name, "%s_%s.ppm", prefix, tag);
    FILE *f = fopen(name, "wb");
    if (!f) { perror(name); return; }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        unsigned char px[3] = { (fb[i] >> 16) & 255, (fb[i] >> 8) & 255, fb[i] & 255 };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static uint32_t trng = 99;
static uint32_t tr(void) { trng ^= trng << 13; trng ^= trng >> 17; trng ^= trng << 5; return trng; }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: see header\n"); return 2; }
    const char *mode = argv[1];

    if (!strcmp(mode, "text")) {
        game_draw_text_test(fb, W, H, W);
        dump(argc > 2 ? argv[2] : "text", "img");
        return 0;
    }

    if (!strcmp(mode, "bench")) {
        game_init(5);
        Input in = { 0, 0 };
        double tu = 0, tr_ = 0, mu = 0, mr = 0; int n = 0;
        for (int fr = 0; fr < 60 * 25; fr++) {
            GameStats s; game_stats(&s);
            in.move = 0; in.held = 0;
            if (s.state != ST_PLAY) in.held = (fr % 30 == 0) ? BTN_START : 0;
            else if (s.nearest_valid) {
                if (fabsf(s.nearest_dx) > 66) in.move = s.nearest_dx > 0 ? 1.0f : -1.0f;
                if (fabsf(s.nearest_dx) < 105 && fr % 20 == 0) in.held |= BTN_PUNCH;
            }
            struct timespec a, b, c;
            clock_gettime(CLOCK_MONOTONIC, &a);
            game_update(&in, 1.0f / 60);
            clock_gettime(CLOCK_MONOTONIC, &b);
            game_render(fb, W, H, W);
            clock_gettime(CLOCK_MONOTONIC, &c);
            double u = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
            double r = (c.tv_sec - b.tv_sec) * 1e3 + (c.tv_nsec - b.tv_nsec) / 1e6;
            tu += u; tr_ += r; n++; if (u > mu) mu = u; if (r > mr) mr = r;
        }
        printf("bench: update avg %.3f ms (max %.3f) | render avg %.3f ms (max %.3f) over %d frames\n", tu / n, mu, tr_ / n, mr, n);
        return 0;
    }

    float secs = argc > 2 ? (float)atof(argv[2]) : 30;
    uint32_t seed = argc > 3 ? (uint32_t)atoi(argv[3]) : 1;
    const char *prefix = argc > 4 ? argv[4] : NULL;
    trng = seed * 7919u + 13;

    game_init(seed);
    Input in = { 0, 0 };
    float dt = 1.0f / 60.0f;
    int frames = (int)(secs * 60);
    int worst_bad = 0, started = 0;
    float worst_stretch = 0;
    int next_dump = 0;
    int atk_toggle = 0, atk_timer = 0;

    for (int fr = 0; fr < frames; fr++) {
        GameStats s; game_stats(&s);
        in.move = 0; in.held = 0;

        if (!strcmp(mode, "afk")) { /* press start once, then do nothing until beaten */
            if (fr == 20) in.held |= BTN_START;
            if (s.state == ST_OVER && fr > 60) { static int over_frames; if (++over_frames > 90) break; }
        } else
        /* title screen: tap start after a moment */
        if (s.state != ST_PLAY) {
            if (!strcmp(mode, "idle") && fr > 120 && s.state == ST_TITLE && started == 0) { /* stay on title for the shot */ }
            else if (fr % 40 == 20) in.held |= BTN_START;
            if (!strcmp(mode, "idle") && s.state == ST_TITLE) in.held &= ~BTN_START;
        }

        if (s.state == ST_PLAY && !strcmp(mode, "bot")) {
            if (s.nearest_valid) {
                float adx = fabsf(s.nearest_dx);
                if (adx > 66) in.move = s.nearest_dx > 0 ? 1.0f : -1.0f;
                else if (adx < 40) in.move = s.nearest_dx > 0 ? -0.6f : 0.6f;
                if (adx < 105 && --atk_timer <= 0) {
                    in.held |= (atk_toggle++ % 3 == 2) ? BTN_KICK : BTN_PUNCH;
                    atk_timer = 14 + tr() % 10;
                }
            }
            if (tr() % 400 == 0) in.held |= BTN_JUMP;
            if (s.player_hp < 35 && tr() % 90 == 0) in.held |= BTN_FLOP;
        } else if (s.state == ST_PLAY && !strcmp(mode, "fuzz")) {
            if (tr() % 30 == 0) in.move = ((int)(tr() % 3) - 1);
            else in.move = (float)((int)(tr() % 3) - 1) * (tr() % 2);
            if (tr() % 6 == 0) in.held |= BTN_PUNCH;
            if (tr() % 9 == 0) in.held |= BTN_KICK;
            if (tr() % 40 == 0) in.held |= BTN_JUMP;
            if (tr() % 70 == 0) in.held |= BTN_FLOP;
        }

        game_update(&in, dt);

        game_stats(&s);
        if (s.state == ST_PLAY) started = 1;
#ifdef GAME_DEBUG
        if (s.bad_points && !worst_bad) { fprintf(stderr, "first bad at frame %d (t=%.2fs)\n", fr, fr / 60.0); extern void game_debug_dump_bad(void); game_debug_dump_bad(); }
#endif
        if (s.bad_points > worst_bad) worst_bad = s.bad_points;
        if (s.max_stretch > worst_stretch) worst_stretch = s.max_stretch;

        if (prefix && fr >= next_dump) {
            game_render(fb, W, H, W);
            char tag[32]; snprintf(tag, sizeof tag, "f%05d", fr);
            dump(prefix, tag);
            next_dump = fr + (argc > 5 ? atoi(argv[5]) : 120);
        }
        if (fr % 600 == 0)
            printf("t=%5.1fs state=%d wave=%d score=%d kills=%d hp=%d enemies=%d bad=%d stretch=%.2f\n",
                   fr / 60.0, s.state, s.wave, s.score, s.kills, s.player_hp, s.enemies, s.bad_points, s.max_stretch);
    }
    if (prefix) { game_render(fb, W, H, W); dump(prefix, "final"); }
    GameStats s; game_stats(&s);
    printf("END mode=%s seed=%u state=%d wave=%d score=%d kills=%d | worst_bad=%d worst_stretch=%.3f\n",
           mode, seed, s.state, s.wave, s.score, s.kills, worst_bad, worst_stretch);
    return worst_bad ? 1 : 0;
}
