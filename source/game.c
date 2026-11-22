/*
 * FLOPPY FIGHTERS - portable game core (see game.h)
 *
 * Every stickman is 11 verlet points joined by fixed-length bones, so the
 * body really is a floppy ragdoll. "Standing" is an invisible, weak balance
 * force that pulls the head up and the hips off the ground; hits (and the
 * Circle-button flop) switch that force off so the stickman crumples, then it
 * fades back in and he pops back up.
 */
#include "game.h"
#include <math.h>
#include <string.h>

#define GROUND  620.0f
#define WALL_L  44.0f
#define WALL_R  1236.0f
#define STEP    (1.0f / 120.0f)
#define GRAV    2300.0f
#define MAX_F   9 /* slot 0 = player */
#define MAX_PART 256
#define MAX_POP 10
#define PI_F    3.14159265f

/* ------------------------------------------------------------------ */
/* data                                                               */
/* ------------------------------------------------------------------ */
enum { P_HEAD, P_NECK, P_HIP, P_ELL, P_HDL, P_ELR, P_HDR, P_KNL, P_FTL, P_KNR, P_FTR, NP };

typedef struct { float x, y, px, py; } Pt;

static const struct { int a, b; float len; } BONES[] = {
    { P_HEAD, P_NECK, 22 }, { P_NECK, P_HIP, 54 },
    { P_NECK, P_ELL, 28 },  { P_ELL, P_HDL, 28 },
    { P_NECK, P_ELR, 28 },  { P_ELR, P_HDR, 28 },
    { P_HIP, P_KNL, 36 },   { P_KNL, P_FTL, 36 },
    { P_HIP, P_KNR, 36 },   { P_KNR, P_FTR, 36 },
};
#define NB ((int)(sizeof(BONES) / sizeof(BONES[0])))

typedef struct {
    int used, is_player, dead, dir, atk, atk_hit, grounded;
    float scale, hp, maxhp, stand, stun, flop, flop_cd, invuln, hurt_flash;
    float phase, atk_t, dead_t, ai_t, speed, dmg_mul, mv, jump_t, cx0, intended;
    uint32_t color;
    Pt p[NP];
} Fighter;

typedef struct { float x, y, vx, vy, life, max; uint32_t col; float size; } Particle;
typedef struct { float x, y, life; const char *txt; } Pop;

static Fighter  F[MAX_F];
static Particle PR[MAX_PART];
static Pop      POP[MAX_POP];

static int   state, wave, score, best, kills, to_spawn;
static float spawn_t, wave_t, banner_t, acc, hitstop, shake, over_t, title_t, clock_t_;
static unsigned prev_held;
static uint32_t rng = 1234567u;

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static float frand(float a, float b) { return a + (b - a) * ((rnd() & 0xFFFFFF) / 16777216.0f); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float absf(float v) { return v < 0 ? -v : v; }

/* balance/walk pulls: translate the point WITHOUT changing its velocity, so the
 * invisible "muscles" can't pump energy into the ragdoll */
static void pullto(Pt *p, float tx, float ty, float k)
{
    float dx = (tx - p->x) * k, dy = (ty - p->y) * k;
    p->x += dx; p->px += dx;
    p->y += dy; p->py += dy;
}

/* attack pulls: move the point and let the integrator turn it into speed (a whip) */
static void whip(Pt *p, float tx, float ty, float k)
{
    p->x += (tx - p->x) * k;
    p->y += (ty - p->y) * k;
}

static float centroid_x(const Fighter *f)
{
    float c = 0;
    for (int i = 0; i < NP; i++) c += f->p[i].x;
    return c / NP;
}

static void spawn_burst(float x, float y, int n, uint32_t col, float speed)
{
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < MAX_PART; j++) {
            if (PR[j].life <= 0) {
                float a = frand(0, 2 * PI_F), s = frand(speed * 0.3f, speed);
                PR[j].x = x; PR[j].y = y;
                PR[j].vx = cosf(a) * s; PR[j].vy = sinf(a) * s - 80;
                PR[j].max = PR[j].life = frand(0.25f, 0.55f);
                PR[j].col = col; PR[j].size = frand(2.5f, 5.5f);
                break;
            }
        }
    }
}

static void spawn_pop(float x, float y)
{
    static const char *words[] = { "POW", "BAM", "WHAM", "BONK", "SMACK", "THWACK" };
    for (int i = 0; i < MAX_POP; i++) {
        if (POP[i].life <= 0) {
            POP[i].x = x; POP[i].y = y; POP[i].life = 0.45f;
            POP[i].txt = words[rnd() % 6];
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* fighters                                                           */
/* ------------------------------------------------------------------ */
static void fighter_reset_pose(Fighter *f, float x)
{
    float s = f->scale, hy = GROUND - 70 * s, ny = hy - 54 * s;
    Pt *P = f->p;
    P[P_HIP].x = x;            P[P_HIP].y = hy;
    P[P_NECK].x = x;           P[P_NECK].y = ny;
    P[P_HEAD].x = x;           P[P_HEAD].y = ny - 22 * s;
    P[P_ELL].x = x - 10 * s;   P[P_ELL].y = ny + 26 * s;
    P[P_HDL].x = x - 12 * s;   P[P_HDL].y = ny + 52 * s;
    P[P_ELR].x = x + 10 * s;   P[P_ELR].y = ny + 26 * s;
    P[P_HDR].x = x + 12 * s;   P[P_HDR].y = ny + 52 * s;
    P[P_KNL].x = x - 5 * s;    P[P_KNL].y = hy + 35 * s;
    P[P_FTL].x = x - 13 * s;   P[P_FTL].y = GROUND - 1;
    P[P_KNR].x = x + 5 * s;    P[P_KNR].y = hy + 35 * s;
    P[P_FTR].x = x + 13 * s;   P[P_FTR].y = GROUND - 1;
    for (int i = 0; i < NP; i++) { P[i].px = P[i].x; P[i].py = P[i].y; }
}

static Fighter *spawn_fighter(int slot, float x, float scale, int is_player,
                              uint32_t color, float hp, float speed, float dmg_mul)
{
    Fighter *f = &F[slot];
    memset(f, 0, sizeof(*f));
    f->used = 1; f->is_player = is_player; f->scale = scale; f->color = color;
    f->hp = f->maxhp = hp; f->speed = speed; f->dmg_mul = dmg_mul;
    f->dir = x < GW / 2 ? 1 : -1; f->stand = 1;
    f->ai_t = frand(0.8f, 1.8f);
    fighter_reset_pose(f, x);
    return f;
}

static void start_attack(Fighter *f, int type)
{
    f->atk = type; f->atk_t = 0; f->atk_hit = 0;
}

static float atk_len(int type) { return type == 1 ? 0.42f : 0.58f; }

static void hurt(Fighter *v, Fighter *a, float dmg, float kx, float ky, float stun, float hx, float hy)
{
    if (v->dead || v->invuln > 0) return;
    if (v->flop > 0) dmg *= 0.5f;
    v->hp -= dmg;
    v->stun = v->stun > stun ? v->stun : stun;
    v->stand = 0; v->hurt_flash = 0.16f; v->invuln = 0.10f;
    v->atk = 0; /* getting hit cancels your attack */
    if (!v->is_player && v->ai_t < 0.5f) v->ai_t = 0.5f;
    for (int i = 0; i < NP; i++) { /* add a velocity impulse to every point */
        v->p[i].px -= kx * STEP;
        v->p[i].py -= ky * STEP;
    }
    spawn_burst(hx, hy, 12, 0xFFD23C, 380);
    spawn_pop(hx, hy - 24);
    hitstop = hitstop > 0.05f ? hitstop : 0.05f;
    shake = clampf(shake + dmg * 0.7f, 0, 14);
    if (v->hp <= 0) {
        v->hp = 0; v->dead = 1; v->dead_t = 0; v->stand = 0;
        spawn_burst(hx, hy, 24, 0xFFFFFF, 520);
        shake = 16; hitstop = 0.09f;
        if (!v->is_player && a && a->is_player) {
            kills++;
            score += 100 + wave * 10;
        }
    }
}

static void check_hits(Fighter *a)
{
    if (!a->atk || a->atk_hit || a->dead) return;
    float lo, hi, dmg, kx, ky, stun;
    Pt *tip;
    if (a->atk == 1) { lo = 0.09f; hi = 0.22f; tip = &a->p[P_HDR]; dmg = 7;  kx = 430; ky = -140; stun = 0.32f; }
    else             { lo = 0.15f; hi = 0.32f; tip = &a->p[P_FTR]; dmg = 12; kx = 820; ky = -420; stun = 0.95f; }
    if (a->atk_t < lo || a->atk_t > hi) return;
    dmg *= a->dmg_mul;
    static const int hitpts[] = { P_HEAD, P_NECK, P_HIP, P_KNL, P_KNR };
    for (int j = 0; j < MAX_F; j++) {
        Fighter *v = &F[j];
        if (!v->used || v->dead || v == a || v->is_player == a->is_player) continue;
        float r = 20 * v->scale + 7;
        for (int k = 0; k < 5; k++) {
            Pt *q = &v->p[hitpts[k]];
            float dx = tip->x - q->x, dy = tip->y - q->y;
            if (dx * dx + dy * dy < r * r) {
                a->atk_hit = 1;
                hurt(v, a, dmg, a->dir * kx, ky, stun, q->x, q->y);
                return;
            }
        }
    }
}

/* per-step control forces: balance, walking animation, attack poses */
static void apply_controls(Fighter *f)
{
    const float h = STEP;
    Pt *P = f->p;
    float sc = f->scale, d = (float)f->dir;
    f->cx0 = centroid_x(f);
    f->intended = 0;

    /* balance strength */
    if (f->dead) f->stand = 0;
    else if (f->stun > 0) { f->stun -= h; f->stand = 0; }
    else if (f->flop > 0) { f->stand = 0; }
    else f->stand = clampf(f->stand + h * 2.8f, 0, 1);
    if (f->flop > 0) f->flop -= h;
    if (f->flop_cd > 0) f->flop_cd -= h;
    if (f->invuln > 0) f->invuln -= h;
    if (f->hurt_flash > 0) f->hurt_flash -= h;
    if (f->dead) f->dead_t += h;

    float s = f->stand;
    float mv = f->mv;
    int can_move = (s > 0.6f) && !f->dead;

    /* walk: pure translation of the whole body, feet animate around it */
    if (can_move && mv != 0) {
        float dx = mv * f->speed * h * (f->atk ? 0.35f : 1.0f);
        for (int i = 0; i < NP; i++) { P[i].x += dx; P[i].px += dx; }
        f->intended += dx;
        f->phase += absf(dx) * (2 * PI_F) / (80.0f * sc);
    } else if (f->grounded && s > 0.6f) {
        f->phase *= 0.98f;
    }
    float walk = (can_move && mv != 0) ? 1.0f : 0.0f;
    float mdir = mv > 0 ? 1.0f : (mv < 0 ? -1.0f : 0.0f);

    if (s > 0.01f) {
        /* hips held at standing height while a foot is planted */
        if (f->grounded) {
            { float dy = ((GROUND - 70 * sc) - P[P_HIP].y) * 0.22f * s; P[P_HIP].y += dy; P[P_HIP].py += dy; }
        } else {
            /* in the air: tuck the legs a bit */
            pullto(&P[P_KNL], P[P_HIP].x - 6 * sc, P[P_HIP].y + 26 * sc, 0.06f * s);
            pullto(&P[P_KNR], P[P_HIP].x + 6 * sc, P[P_HIP].y + 26 * sc, 0.06f * s);
        }
        /* upright spine (lean slightly into movement and attacks) */
        float lean = mdir * 7 * sc + (f->atk ? d * 6 * sc : 0);
        pullto(&P[P_NECK], P[P_HIP].x + lean * 0.5f, P[P_HIP].y - 54 * sc, 0.30f * s);
        pullto(&P[P_HEAD], P[P_HIP].x + lean,        P[P_HIP].y - 76 * sc, 0.30f * s);

        /* legs: feet step around the hips */
        for (int side = 0; side < 2; side++) {
            Pt *ft = &P[side ? P_FTR : P_FTL];
            Pt *kn = &P[side ? P_KNR : P_KNL];
            float off = side ? PI_F : 0;
            float sx = (side ? 1.0f : -1.0f) * 13 * sc;
            float tx = P[P_HIP].x + sx + walk * mdir * sinf(f->phase + off) * 24 * sc;
            float lift = walk * fmaxf(0.0f, cosf(f->phase + off)) * 15 * sc;
            int kicking = (f->atk == 2 && side == 1);
            if (!kicking) {
                { float dx = (tx - ft->x) * 0.28f * s; ft->x += dx; ft->px += dx; }
                if (lift > 0.5f && f->grounded) { float dy = ((GROUND - lift) - ft->y) * 0.6f * s; ft->y += dy; ft->py += dy; }
            }
            pullto(kn, (P[P_HIP].x + ft->x) * 0.5f + d * 6 * sc, (P[P_HIP].y + ft->y) * 0.5f, 0.15f * s);
        }

        /* arms: weak "hanging" pull so they swing loosely */
        for (int side = 0; side < 2; side++) {
            Pt *el = &P[side ? P_ELR : P_ELL];
            Pt *hd = &P[side ? P_HDR : P_HDL];
            float sx = side ? 1.0f : -1.0f;
            float sw = walk * mdir * sinf(f->phase + (side ? 0 : PI_F)) * 16 * sc;
            if (f->atk == 1 && side == 1) continue; /* attack arm handled below */
            pullto(el, P[P_NECK].x + sx * 9 * sc + sw * 0.5f, P[P_NECK].y + 25 * sc, 0.07f * s);
            pullto(hd, P[P_NECK].x + sx * 12 * sc + sw,       P[P_NECK].y + 50 * sc, 0.07f * s);
        }
    }

    /* attacks (these act even while stand is dipping, but not when dead) */
    if (f->atk && !f->dead) {
        float t = f->atk_t, nx = P[P_NECK].x, ny = P[P_NECK].y;
        float hx = P[P_HIP].x, hy = P[P_HIP].y;
        if (f->atk == 1) {
            Pt *el = &P[P_ELR], *hd = &P[P_HDR];
            if (t < 0.07f) {
                whip(el, nx - d * 10 * sc, ny + 20 * sc, 0.35f);
                whip(hd, nx - d * 16 * sc, ny + 10 * sc, 0.35f);
            } else if (t < 0.22f) {
                whip(el, nx + d * 30 * sc, ny + 4 * sc, 0.55f);
                whip(hd, nx + d * 58 * sc, ny + 2 * sc, 0.75f);
                for (int i = 0; i < NP; i++) { if (i == P_FTL || i == P_FTR) continue; P[i].x += d * 55 * sc * h; P[i].px += d * 55 * sc * h; }
                f->intended += d * 55 * sc * h * (NP - 2) / NP;
            }
            /* recovery (t >= 0.22): arm just goes limp again */
        } else {
            Pt *ft = &P[P_FTR], *kn = &P[P_KNR];
            if (t < 0.15f) {
                whip(kn, hx - d * 8 * sc, hy + 6 * sc, 0.30f);
                whip(ft, hx - d * 12 * sc, GROUND - 26 * sc, 0.30f);
            } else if (t < 0.34f) {
                whip(kn, hx + d * 34 * sc, hy - 2 * sc, 0.55f);
                whip(ft, hx + d * 68 * sc, hy - 26 * sc, 0.75f);
                for (int i = 0; i < NP; i++) { if (i == P_FTL || i == P_FTR) continue; P[i].x += d * 45 * sc * h; P[i].px += d * 45 * sc * h; }
                f->intended += d * 45 * sc * h * (NP - 2) / NP;
            }
        }
        f->atk_t += h;
        if (f->atk_t >= atk_len(f->atk)) f->atk = 0;
    }
}

/* ------------------------------------------------------------------ */
/* physics                                                            */
/* ------------------------------------------------------------------ */
static void collide(Fighter *f)
{
    Pt *P = f->p;
    for (int i = 0; i < NP; i++) {
        float r = (i == P_HEAD) ? 14 * f->scale : 0;
        if (P[i].y > GROUND - r) P[i].y = GROUND - r;
        if (P[i].x < WALL_L) { P[i].x = WALL_L; P[i].px = P[i].x; } /* walls absorb sideways speed */
        if (P[i].x > WALL_R) { P[i].x = WALL_R; P[i].px = P[i].x; }
    }
}

static void integrate_and_constrain(Fighter *f)
{
    Pt *P = f->p;
    float sc = f->scale;
    float g = GRAV * STEP * STEP;
    float damp = f->dead ? 0.992f : 0.985f;
    /* how much of its weight the "muscles" carry while balancing (0 = limp) */
    static const float SUPPORT[NP] = {
        /*HEAD*/0.96f, /*NECK*/0.96f, /*HIP*/0.92f, /*ELL*/0.0f, /*HDL*/0.0f, /*ELR*/0.0f, /*HDR*/0.0f,
        /*KNL*/0.88f, /*FTL*/0.0f, /*KNR*/0.88f, /*FTR*/0.0f };
    float st = f->dead ? 0.0f : f->stand;
    for (int i = 0; i < NP; i++) {
        float dm = damp - 0.14f * SUPPORT[i] * st; /* muscles also damp motion */
        float vx = (P[i].x - P[i].px) * dm;
        float vy = (P[i].y - P[i].py) * dm;
        P[i].px = P[i].x; P[i].py = P[i].y;
        P[i].x += vx;     P[i].y += vy + g * (1.0f - SUPPORT[i] * st);
    }
    for (int it = 0; it < 10; it++) {
        for (int b = 0; b < NB; b++) {
            Pt *A = &P[BONES[b].a], *B = &P[BONES[b].b];
            float L = BONES[b].len * sc;
            float dx = B->x - A->x, dy = B->y - A->y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 1e-4f) continue;
            float k = (d - L) / d * 0.5f;
            A->x += dx * k; A->y += dy * k;
            B->x -= dx * k; B->y -= dy * k;
        }
        collide(f);
    }
    /* ground friction, once per step: strong while balancing, weak when limp */
    float fr = (f->stand > 0.5f && !f->dead) ? 0.55f : 0.10f;
    for (int i = 0; i < NP; i++) {
        float r = (i == P_HEAD) ? 14 * sc : 0;
        if (P[i].y >= GROUND - r - 0.01f) P[i].px = P[i].x - (P[i].x - P[i].px) * (1.0f - fr);
    }
    float lowest = P[P_FTL].y > P[P_FTR].y ? P[P_FTL].y : P[P_FTR].y;
    f->grounded = lowest >= GROUND - 6;
    /* traction: while balancing on the ground, bleed off the body's overall sideways
     * drift (limbs can still swing); walking/knockback are handled elsewhere */
    if (f->grounded && st > 0.5f) {
        float mvx = 0;
        for (int i = 0; i < NP; i++) mvx += P[i].x - P[i].px;
        mvx /= NP;
        for (int i = 0; i < NP; i++) P[i].px += mvx * 0.35f;
    }
    /* plant: while balancing, the body's centre may only move by the walk/lunge we
     * asked for - this stops the soft "muscle" pulls from creeping the body sideways */
    if (f->grounded && st > 0.5f) {
        float lock = clampf((st - 0.5f) * 2.0f, 0, 1);
        float corr = (f->cx0 + f->intended - centroid_x(f)) * lock;
        for (int i = 0; i < NP; i++) { P[i].x += corr; P[i].px += corr; }
        collide(f); /* the planting shift must never push a limb through a wall */
    }
}

static void separate_fighters(void)
{
    for (int i = 0; i < MAX_F; i++) {
        Fighter *a = &F[i];
        if (!a->used || a->dead || a->stand < 0.3f) continue;
        for (int j = i + 1; j < MAX_F; j++) {
            Fighter *b = &F[j];
            if (!b->used || b->dead || b->stand < 0.3f) continue;
            if (a->p[P_HIP].y < GROUND - 110 || b->p[P_HIP].y < GROUND - 110) continue; /* jumped over */
            float dx = b->p[P_HIP].x - a->p[P_HIP].x;
            float minD = 30 * (a->scale + b->scale);
            if (absf(dx) < minD) {
                float push = (minD - absf(dx)) * 0.5f * (dx >= 0 ? 1.0f : -1.0f);
                for (int k = 0; k < NP; k++) {
                    a->p[k].x -= push; a->p[k].px -= push;
                    b->p[k].x += push; b->p[k].px += push;
                }
            }
        }
    }
}

static void physics_step(void)
{
    for (int i = 0; i < MAX_F; i++) {
        if (!F[i].used) continue;
        apply_controls(&F[i]);
        integrate_and_constrain(&F[i]);
    }
    for (int i = 0; i < MAX_F; i++) if (F[i].used) check_hits(&F[i]);
    separate_fighters();
}

static void do_jump(Fighter *f, float v)
{
    for (int i = 0; i < NP; i++) f->p[i].py = f->p[i].y + v * STEP;
}

/* ------------------------------------------------------------------ */
/* brains                                                             */
/* ------------------------------------------------------------------ */
static int alive_enemies(void)
{
    int n = 0;
    for (int i = 1; i < MAX_F; i++) if (F[i].used && !F[i].dead) n++;
    return n;
}

static Fighter *nearest_enemy(Fighter *from, float maxd)
{
    Fighter *best_f = 0;
    float bd = maxd;
    for (int i = 0; i < MAX_F; i++) {
        Fighter *e = &F[i];
        if (!e->used || e->dead || e->is_player == from->is_player) continue;
        float d = absf(e->p[P_HIP].x - from->p[P_HIP].x);
        if (d < bd) { bd = d; best_f = e; }
    }
    return best_f;
}

static void think_player(const Input *in, unsigned pressed)
{
    Fighter *p = &F[0];
    p->mv = 0;
    if (p->dead) return;
    p->mv = clampf(in->move, -1, 1);
    int can_act = p->stun <= 0 && p->flop <= 0 && p->stand > 0.5f;
    if (can_act && p->atk == 0) {
        if (p->mv != 0) p->dir = p->mv > 0 ? 1 : -1;
        if (pressed & (BTN_PUNCH | BTN_KICK)) {
            Fighter *e = nearest_enemy(p, 260);
            if (e) p->dir = e->p[P_HIP].x >= p->p[P_HIP].x ? 1 : -1; /* aim assist */
            start_attack(p, (pressed & BTN_KICK) ? 2 : 1);
        } else if ((pressed & BTN_JUMP) && p->grounded) {
            do_jump(p, 640);
        }
    }
    if ((pressed & BTN_FLOP) && p->flop_cd <= 0) {
        p->flop = 0.9f; p->flop_cd = 2.0f; p->atk = 0;
    }
}

static void think_enemy(Fighter *e, float dt)
{
    Fighter *pl = &F[0];
    e->mv = 0;
    if (e->dead || pl->dead || state != ST_PLAY) return;
    if (e->stun > 0 || e->stand < 0.6f || e->flop > 0) return;
    float dx = pl->p[P_HIP].x - e->p[P_HIP].x, ad = absf(dx);
    float range = 78 * (e->scale > pl->scale ? e->scale : pl->scale);
    if (!e->atk) {
        e->dir = dx >= 0 ? 1 : -1;
        if (ad > range)              e->mv = dx > 0 ? 1.0f : -1.0f;
        else if (ad < range * 0.55f) e->mv = dx > 0 ? -0.5f : 0.5f; /* back off a touch */
    }
    e->ai_t -= dt;
    if (e->ai_t <= 0 && !e->atk && ad < range + 18) {
        int kick = (wave >= 2) && (rnd() % 100 < 30);
        start_attack(e, kick ? 2 : 1);
        float slow = 1.0f - clampf(wave * 0.04f, 0, 0.45f);
        e->ai_t = frand(0.8f, 1.7f) * slow;
    }
}

/* ------------------------------------------------------------------ */
/* game flow                                                          */
/* ------------------------------------------------------------------ */
static void reset_game(void)
{
    memset(F, 0, sizeof(F)); memset(PR, 0, sizeof(PR)); memset(POP, 0, sizeof(POP));
    spawn_fighter(0, 300, 1.0f, 1, 0x2E6BFF, 100, 270, 1.0f);
    state = ST_PLAY; wave = 0; score = 0; kills = 0; to_spawn = 0;
    wave_t = 0.7f; banner_t = 0; spawn_t = 0; over_t = 0; hitstop = 0; shake = 0; acc = 0;
}

static void start_wave(void)
{
    wave++;
    to_spawn = 1 + wave;
    if (to_spawn > 12) to_spawn = 12;
    spawn_t = 0.2f;
    banner_t = 1.8f;
}

static void spawn_enemy(void)
{
    static const uint32_t cols[] = { 0x1B1B1B, 0xD6342C, 0x8A3FD1, 0x1F9E6B, 0xE08A00 };
    int slot = -1;
    for (int i = 1; i < MAX_F; i++) if (!F[i].used) { slot = i; break; }
    if (slot < 0) return;
    int brute = (wave % 3 == 0) && (to_spawn == 1 + wave || to_spawn == 1);
    float px = F[0].p[P_HIP].x;
    float x = (rnd() & 1) ? 1150.0f : 130.0f;
    if (absf(x - px) < 320) x = px < GW / 2 ? 1150.0f : 130.0f;
    Fighter *e;
    if (brute) e = spawn_fighter(slot, x, 1.3f, 0, 0x8B1E1E, 90 + wave * 4, 95, 1.5f);
    else {
        float hp = 34 + wave * 3; if (hp > 75) hp = 75;
        float sp = 105 + wave * 6; if (sp > 185) sp = 185;
        e = spawn_fighter(slot, x, 1.0f, 0, cols[rnd() % 5], hp, sp, 1.0f + wave * 0.03f);
    }
    /* drop in from the sky and flop on landing */
    for (int i = 0; i < NP; i++) { e->p[i].y -= 420; e->p[i].py -= 420; }
    e->stun = 0.5f; e->stand = 0;
}

void game_init(uint32_t seed)
{
    rng = seed ? seed : 1u;
    memset(F, 0, sizeof(F)); memset(PR, 0, sizeof(PR)); memset(POP, 0, sizeof(POP));
    spawn_fighter(0, GW / 2.0f, 1.0f, 1, 0x2E6BFF, 100, 270, 1.0f);
    state = ST_TITLE; wave = score = kills = to_spawn = 0; best = 0;
    wave_t = banner_t = spawn_t = acc = hitstop = shake = over_t = 0;
    title_t = 0; clock_t_ = 0; prev_held = 0;
}

void game_update(const Input *in, float dt)
{
    unsigned pressed = in->held & ~prev_held;
    prev_held = in->held;
    dt = clampf(dt, 0, 1.0f / 15.0f);
    clock_t_ += dt;

    /* particles, pops, shake always run */
    for (int i = 0; i < MAX_PART; i++) {
        if (PR[i].life <= 0) continue;
        PR[i].life -= dt; PR[i].x += PR[i].vx * dt; PR[i].y += PR[i].vy * dt;
        PR[i].vy += 1300 * dt;
    }
    for (int i = 0; i < MAX_POP; i++) if (POP[i].life > 0) { POP[i].life -= dt; POP[i].y -= 40 * dt; }
    shake = shake - 45 * dt; if (shake < 0) shake = 0;
    if (banner_t > 0) banner_t -= dt;

    if (state == ST_TITLE) {
        static float next_flop = 1.2f;
        title_t += dt;
        F[0].mv = 0;
        if (title_t > next_flop) { F[0].flop = 0.9f; next_flop += 3.4f; }
        if (pressed & BTN_START) { reset_game(); next_flop = 1.2f; return; }
    } else if (state == ST_PLAY) {
        think_player(in, pressed);
        for (int i = 1; i < MAX_F; i++) if (F[i].used) think_enemy(&F[i], dt);

        if (wave_t > 0) {
            wave_t -= dt;
            if (wave_t <= 0) { start_wave(); wave_t = -1; }
        } else if (to_spawn == 0 && alive_enemies() == 0 && !F[0].dead) {
            wave_t = 2.2f;
            F[0].hp = fminf(F[0].maxhp, F[0].hp + 25);
        }
        if (to_spawn > 0) {
            spawn_t -= dt;
            int cap = wave >= 6 ? 5 : 4;
            if (spawn_t <= 0 && alive_enemies() < cap) {
                spawn_enemy(); to_spawn--; spawn_t = frand(0.6f, 1.4f);
            }
        }
        if (F[0].dead) {
            over_t += dt;
            if (over_t > 1.8f) { state = ST_OVER; if (score > best) best = score; }
        }
    } else { /* ST_OVER */
        F[0].mv = 0;
        for (int i = 1; i < MAX_F; i++) F[i].mv = 0;
        if (pressed & BTN_START) { reset_game(); return; }
    }

    if (hitstop > 0) { hitstop -= dt; return; }
    acc += dt;
    int guard = 0;
    while (acc >= STEP && guard++ < 10) { acc -= STEP; physics_step(); }
    if (acc > STEP) acc = 0;

    for (int i = 1; i < MAX_F; i++)
        if (F[i].used && F[i].dead && F[i].dead_t > 3.3f) F[i].used = 0;
}

void game_stats(GameStats *s)
{
    memset(s, 0, sizeof(*s));
    s->state = state; s->wave = wave; s->score = score; s->kills = kills;
    s->player_hp = (int)F[0].hp; s->enemies = alive_enemies();
    s->player_x = F[0].p[P_HIP].x;
    float bd = 1e9f;
    for (int i = 1; i < MAX_F; i++) {
        if (!F[i].used || F[i].dead) continue;
        float dx = F[i].p[P_HIP].x - F[0].p[P_HIP].x;
        if (absf(dx) < bd) { bd = absf(dx); s->nearest_dx = dx; s->nearest_valid = 1; }
    }
    for (int i = 0; i < MAX_F; i++) {
        if (!F[i].used) continue;
        for (int k = 0; k < NP; k++) {
            float x = F[i].p[k].x, y = F[i].p[k].y;
            if (!(x == x) || !(y == y) || x < 0 || x > GW || y < -700 || y > GH) s->bad_points++;
        }
        for (int b = 0; b < NB; b++) {
            Pt *A = &F[i].p[BONES[b].a], *B = &F[i].p[BONES[b].b];
            float dx = B->x - A->x, dy = B->y - A->y;
            float r = sqrtf(dx * dx + dy * dy) / (BONES[b].len * F[i].scale);
            if (r > s->max_stretch) s->max_stretch = r;
        }
    }
}

/* ------------------------------------------------------------------ */
/* software renderer                                                  */
/* ------------------------------------------------------------------ */
static uint32_t *R_fb;
static int R_w, R_h, R_pitch;
static float R_sc, R_ox, R_oy, R_shx, R_shy;

static inline void put(int x, int y, uint32_t c, float a)
{
    if ((unsigned)x >= (unsigned)R_w || (unsigned)y >= (unsigned)R_h || a <= 0.0f) return;
    uint32_t *d = &R_fb[y * R_pitch + x];
    if (a >= 0.996f) { *d = c; return; }
    int ia = (int)(a * 256.0f);
    uint32_t o = *d;
    uint32_t r = (((o >> 16) & 255) * (256 - ia) + ((c >> 16) & 255) * ia) >> 8;
    uint32_t g = (((o >> 8) & 255) * (256 - ia) + ((c >> 8) & 255) * ia) >> 8;
    uint32_t b = ((o & 255) * (256 - ia) + (c & 255) * ia) >> 8;
    *d = (r << 16) | (g << 8) | b;
}

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    t = clampf(t, 0, 1);
    int it = (int)(t * 256);
    uint32_t r = (((a >> 16) & 255) * (256 - it) + ((b >> 16) & 255) * it) >> 8;
    uint32_t g = (((a >> 8) & 255) * (256 - it) + ((b >> 8) & 255) * it) >> 8;
    uint32_t bl = ((a & 255) * (256 - it) + (b & 255) * it) >> 8;
    return (r << 16) | (g << 8) | bl;
}

/* logical -> screen */
static float SX(float x) { return (x + R_shx) * R_sc + R_ox; }
static float SY(float y) { return (y + R_shy) * R_sc + R_oy; }

static void rect(float x0, float y0, float x1, float y1, uint32_t c, float a)
{
    int ix0 = (int)SX(x0), iy0 = (int)SY(y0), ix1 = (int)SX(x1), iy1 = (int)SY(y1);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > R_w) ix1 = R_w;
    if (iy1 > R_h) iy1 = R_h;
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) put(x, y, c, a);
}

/* anti-aliased capsule (a thick line with round caps); r = 0 -> use as disc w/ a==b */
static void capsule(float ax, float ay, float bx, float by, float r, uint32_t c, float alpha)
{
    ax = SX(ax); ay = SY(ay); bx = SX(bx); by = SY(by); r *= R_sc;
    int x0 = (int)(fminf(ax, bx) - r - 1), x1 = (int)(fmaxf(ax, bx) + r + 2);
    int y0 = (int)(fminf(ay, by) - r - 1), y1 = (int)(fmaxf(ay, by) + r + 2);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > R_w) x1 = R_w;
    if (y1 > R_h) y1 = R_h;
    float vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f - ax, py = y + 0.5f - ay;
            float t = l2 > 1e-6f ? clampf((px * vx + py * vy) / l2, 0, 1) : 0;
            float dx = px - t * vx, dy = py - t * vy;
            float d = sqrtf(dx * dx + dy * dy) - r;
            float cov = 0.5f - d;
            if (cov > 0) put(x, y, c, (cov > 1 ? 1 : cov) * alpha);
        }
    }
}

static void disc(float cx, float cy, float r, uint32_t c, float alpha) { capsule(cx, cy, cx, cy, r, c, alpha); }

static void ring(float cx, float cy, float r, float thick, uint32_t c, float alpha)
{
    cx = SX(cx); cy = SY(cy); r *= R_sc; thick *= R_sc;
    int x0 = (int)(cx - r - thick), x1 = (int)(cx + r + thick + 2);
    int y0 = (int)(cy - r - thick), y1 = (int)(cy + r + thick + 2);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > R_w) x1 = R_w;
    if (y1 > R_h) y1 = R_h;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d = absf(sqrtf(dx * dx + dy * dy) - r) - thick * 0.5f;
            float cov = 0.5f - d;
            if (cov > 0) put(x, y, c, (cov > 1 ? 1 : cov) * alpha);
        }
}

static void ellipse(float cx, float cy, float rx, float ry, uint32_t c, float alpha)
{
    cx = SX(cx); cy = SY(cy); rx *= R_sc; ry *= R_sc;
    for (int y = (int)(cy - ry); y <= (int)(cy + ry); y++)
        for (int x = (int)(cx - rx); x <= (int)(cx + rx); x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            float d2 = dx * dx + dy * dy;
            if (d2 < 1) put(x, y, c, alpha * (1.0f - d2));
        }
}

/* ---- 5x7 bitmap font, ASCII 32..95, one byte per column, bit0 = top row ---- */
static const uint8_t FONT[64][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x56,0x20,0x50},{0x00,0x08,0x07,0x03,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
};

static int text_w(const char *s, int sc) { int n = 0; while (s[n]) n++; return n ? n * 6 * sc - sc : 0; }

static void text(float x, float y, const char *s, int sc, uint32_t c, float alpha)
{
    for (; *s; s++, x += 6 * sc) {
        int ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 95) ch = '?';
        const uint8_t *g = FONT[ch - 32];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (g[col] & (1 << row))
                    rect(x + col * sc, y + row * sc, x + (col + 1) * sc, y + (row + 1) * sc, c, alpha);
    }
}

static void text_c(float y, const char *s, int sc, uint32_t c, float alpha)
{
    float x = (GW - text_w(s, sc)) * 0.5f;
    text(x + sc, y + sc, s, sc, 0x101820, alpha * 0.55f); /* drop shadow */
    text(x, y, s, sc, c, alpha);
}

static void num_to_str(char *out, const char *prefix, int v)
{
    char tmp[16]; int n = 0, i = 0;
    while (*prefix) out[i++] = *prefix++;
    if (v <= 0) tmp[n++] = '0';
    while (v > 0 && n < 15) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

/* ---- scene ---- */
static void draw_background(void)
{
    for (int y = 0; y < R_h; y++) {
        float ly = (y - R_oy) / R_sc;
        uint32_t c;
        if (ly < 0 || ly >= GH) c = 0x000000;
        else if (ly < GROUND) c = mix(0xB9D3EE, 0xF7EFDD, ly / GROUND);
        else c = mix(0x4A4F5E, 0x262A35, (ly - GROUND) / (GH - GROUND));
        uint32_t *row = &R_fb[y * R_pitch];
        for (int x = 0; x < R_w; x++) row[x] = c;
    }
    /* sun + skyline */
    disc(980, 150, 70, 0xFFF3C4, 0.85f);
    for (int i = 0; i < 16; i++) {
        uint32_t hsh = (uint32_t)(i * 2654435761u) >> 8;
        float bw = 60 + (hsh & 31), bh = 90 + ((hsh >> 5) & 127);
        float bx = i * 82.0f - 20;
        rect(bx, GROUND - bh, bx + bw, GROUND, 0x9DB4CF, 0.55f);
    }
    /* floor: top edge highlight + lane stripes */
    rect(0, GROUND, GW, GROUND + 4, 0x8B93A8, 1.0f);
    for (int i = 0; i < 14; i++) rect(i * 100.0f + 20, GROUND + 38, i * 100.0f + 70, GROUND + 42, 0x5C6275, 0.7f);
    /* arena walls */
    rect(0, 0, WALL_L - 14, GH, 0x1C2030, 0.9f);
    rect(WALL_R + 14, 0, GW, GH, 0x1C2030, 0.9f);
}

/* The background never changes, so it is drawn once into a cache and block-copied
 * every frame (the PS3's PPU is slow, so every saved pixel matters). */
#define BG_MAX (1920 * 1080)
static uint32_t BG[BG_MAX];
static int   bg_w, bg_h;
static float bg_sc;

static void blit_background(void)
{
    if ((long)R_w * R_h > BG_MAX) { draw_background(); return; }
    if (bg_w != R_w || bg_h != R_h || bg_sc != R_sc) {
        uint32_t *fb = R_fb; int pitch = R_pitch; float sx = R_shx, sy = R_shy;
        R_fb = BG; R_pitch = R_w; R_shx = R_shy = 0;
        draw_background();
        R_fb = fb; R_pitch = pitch; R_shx = sx; R_shy = sy;
        bg_w = R_w; bg_h = R_h; bg_sc = R_sc;
    }
    for (int y = 0; y < R_h; y++) memcpy(&R_fb[y * R_pitch], &BG[y * R_w], (size_t)R_w * sizeof(uint32_t));
}

static void draw_fighter(Fighter *f)
{
    Pt *P = f->p;
    float sc = f->scale;
    float alpha = 1.0f;
    if (f->dead) alpha = 1.0f - clampf((f->dead_t - 2.4f) / 0.8f, 0, 1);
    if (alpha <= 0) return;
    uint32_t col = f->color;
    if (f->hurt_flash > 0) col = mix(col, 0xFFFFFF, f->hurt_flash / 0.16f);

    float hy = P[P_HIP].y;
    float air = clampf((GROUND - hy - 40 * sc) / 140.0f, 0, 1);
    ellipse(P[P_HIP].x, GROUND + 6, 38 * sc * (1 - 0.5f * air), 8 * sc, 0x000000, 0.35f * alpha);

    float th = 6.5f * sc;
    for (int b = 0; b < NB; b++) {
        float w = (b == 1) ? th + 1.5f * sc : th;
        capsule(P[BONES[b].a].x, P[BONES[b].a].y, P[BONES[b].b].x, P[BONES[b].b].y, w * 0.5f, col, alpha);
    }
    /* hands/feet knobs */
    disc(P[P_HDL].x, P[P_HDL].y, th * 0.62f, col, alpha);
    disc(P[P_HDR].x, P[P_HDR].y, th * 0.62f, col, alpha);

    /* head */
    float hx = P[P_HEAD].x, hyy = P[P_HEAD].y, d = (float)f->dir;
    disc(hx, hyy, 14 * sc, 0xFBFBFB, alpha);
    ring(hx, hyy, 14.5f * sc, 4 * sc, col, alpha);
    if (f->dead || (f->stun > 0.25f && f->hp < f->maxhp * 0.35f)) {
        for (int e = 0; e < 2; e++) {
            float ex = hx + d * (3 + e * 6.5f) * sc, ey = hyy - 2 * sc, s = 2.2f * sc;
            capsule(ex - s, ey - s, ex + s, ey + s, 0.7f * sc, 0x202020, alpha);
            capsule(ex - s, ey + s, ex + s, ey - s, 0.7f * sc, 0x202020, alpha);
        }
    } else {
        float blink = (fmodf(clock_t_ + f->scale * 3.1f, 3.3f) > 3.2f) ? 0.3f : 1.8f;
        disc(hx + d * 3 * sc,   hyy - 2 * sc, blink * sc, 0x202020, alpha);
        disc(hx + d * 9.5f * sc, hyy - 2 * sc, blink * sc, 0x202020, alpha);
    }
    if (f->is_player) { /* red bandana with tails */
        capsule(hx - 13 * sc, hyy - 8 * sc, hx + 13 * sc, hyy - 8 * sc, 2.4f * sc, 0xE63946, alpha);
        float wag = sinf(clock_t_ * 9) * 3 * sc;
        capsule(hx - d * 12 * sc, hyy - 8 * sc, hx - d * 26 * sc, hyy - 4 * sc + wag, 1.8f * sc, 0xE63946, alpha);
        capsule(hx - d * 12 * sc, hyy - 8 * sc, hx - d * 24 * sc, hyy - 12 * sc - wag, 1.8f * sc, 0xE63946, alpha);
    }
    /* dizzy stars while stunned */
    if (!f->dead && f->stun > 0.15f) {
        for (int i = 0; i < 3; i++) {
            float a = clock_t_ * 7 + i * 2.094f;
            disc(hx + cosf(a) * 20 * sc, hyy - 22 * sc + sinf(a) * 5 * sc, 2.6f * sc, 0xFFD23C, alpha);
        }
    }
    /* enemy health bar */
    if (!f->is_player && !f->dead && f->hp < f->maxhp) {
        float bw = 52 * sc, bx = hx - bw / 2, by = hyy - 34 * sc;
        rect(bx - 1, by - 1, bx + bw + 1, by + 6, 0x000000, 0.6f);
        rect(bx, by, bx + bw * (f->hp / f->maxhp), by + 5, 0xE5484D, 1.0f);
    }
}

static void draw_hud(void)
{
    Fighter *p = &F[0];
    char buf[40];
    /* player health */
    float ratio = clampf(p->hp / p->maxhp, 0, 1);
    rect(40, 28, 420, 58, 0x000000, 0.55f);
    rect(44, 32, 44 + 372 * ratio, 54, mix(0xE5484D, 0x3DDC84, ratio), 1.0f);
    text(52, 36, "HP", 2, 0xFFFFFF, 0.9f);
    num_to_str(buf, "SCORE ", score);
    text(1240 - text_w(buf, 3), 30, buf, 3, 0x1C2030, 1.0f);
    num_to_str(buf, "BEST ", best > score ? best : score);
    text(1240 - text_w(buf, 2), 62, buf, 2, 0x3B4252, 1.0f);
    num_to_str(buf, "WAVE ", wave);
    text(44, 68, buf, 3, 0x1C2030, 1.0f);
    /* flop cooldown pip */
    if (p->flop_cd > 0) rect(44, 100, 44 + 120 * clampf(p->flop_cd / 2.0f, 0, 1), 106, 0x3B4252, 0.8f);
}

void game_render(uint32_t *fb, int w, int h, int pitch_px)
{
    R_fb = fb; R_w = w; R_h = h; R_pitch = pitch_px;
    float sx = (float)w / GW, sy = (float)h / GH;
    R_sc = sx < sy ? sx : sy;
    R_ox = (w - GW * R_sc) * 0.5f; R_oy = (h - GH * R_sc) * 0.5f;
    uint32_t keep = rng; /* screen shake must not disturb gameplay randomness */
    float wsx = shake > 0.01f ? frand(-1, 1) * shake : 0;
    float wsy = shake > 0.01f ? frand(-1, 1) * shake : 0;
    R_shx = R_shy = 0;

    blit_background();
    R_shx = wsx; R_shy = wsy; /* only the fighters and sparks shake, not the HUD */

    for (int i = MAX_F - 1; i >= 0; i--) if (F[i].used && F[i].dead) draw_fighter(&F[i]);
    for (int i = MAX_F - 1; i >= 1; i--) if (F[i].used && !F[i].dead) draw_fighter(&F[i]);
    if (F[0].used && !F[0].dead) draw_fighter(&F[0]);

    for (int i = 0; i < MAX_PART; i++) {
        if (PR[i].life <= 0) continue;
        float t = PR[i].life / PR[i].max;
        disc(PR[i].x, PR[i].y, PR[i].size * (0.4f + 0.6f * t), PR[i].col, clampf(t * 1.5f, 0, 1));
    }
    for (int i = 0; i < MAX_POP; i++) {
        if (POP[i].life <= 0) continue;
        float t = POP[i].life / 0.45f;
        int sc = t > 0.7f ? 6 : 5;
        float x = POP[i].x - text_w(POP[i].txt, sc) / 2.0f;
        text(x + 2, POP[i].y + 2, POP[i].txt, sc, 0x101820, clampf(t * 1.4f, 0, 1));
        text(x, POP[i].y, POP[i].txt, sc, 0xFFD23C, clampf(t * 1.4f, 0, 1));
    }

    R_shx = R_shy = 0;
    if (state == ST_TITLE) {
        float bob = sinf(clock_t_ * 2.0f) * 6;
        text_c(50 + bob, "FLOPPY", 14, 0x2E6BFF, 1.0f);
        text_c(165 + bob, "FIGHTERS", 14, 0xE63946, 1.0f);
        text_c(290, "STICK MAN BRAWLER", 3, 0x1C2030, 0.9f);
        text_c(335, "STICK/DPAD MOVE   SQUARE PUNCH   TRIANGLE KICK", 2, 0x1C2030, 0.9f);
        text_c(362, "CROSS JUMP   CIRCLE GO LIMP AND TAKE HALF DAMAGE", 2, 0x1C2030, 0.9f);
        if (fmodf(clock_t_, 1.2f) < 0.8f) text_c(398, "PRESS START", 5, 0x1C2030, 1.0f);
    } else {
        draw_hud();
        if (state == ST_PLAY && banner_t > 0)
            text_c(220, wave_t > 0 && wave > 0 ? "WAVE CLEAR" : "FIGHT", 9, 0xFFFFFF, clampf(banner_t, 0, 1));
        if (state == ST_PLAY && wave_t > 0 && wave > 0 && banner_t <= 0) {
            char buf[24]; num_to_str(buf, "WAVE CLEAR  NEXT WAVE ", wave + 1);
            text_c(210, buf, 4, 0x1C2030, 0.9f);
        }
        if (state == ST_OVER) {
            char buf[40];
            rect(0, 0, GW, GH, 0x000000, 0.55f);
            text_c(190, "YOU GOT FLOPPED", 9, 0xFFFFFF, 1.0f);
            num_to_str(buf, "SCORE ", score);
            text_c(330, buf, 5, 0xFFD23C, 1.0f);
            num_to_str(buf, "WAVE REACHED ", wave);
            text_c(390, buf, 3, 0xFFFFFF, 1.0f);
            num_to_str(buf, "BEST ", best);
            text_c(430, buf, 3, 0xFFFFFF, 1.0f);
            if (fmodf(clock_t_, 1.2f) < 0.8f) text_c(500, "PRESS START TO TRY AGAIN", 4, 0xFFFFFF, 1.0f);
        }
    }
    rng = keep;
}

void game_draw_text_test(uint32_t *fb, int w, int h, int pitch_px)
{
    R_fb = fb; R_w = w; R_h = h; R_pitch = pitch_px;
    R_sc = (float)w / GW; R_ox = R_oy = R_shx = R_shy = 0;
    for (int i = 0; i < w * h; i++) fb[i] = 0xF0F0F0;
    text(20, 20,  "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 4, 0x101010, 1);
    text(20, 80,  "0123456789 !\"#$%&'()*+,-./", 4, 0x101010, 1);
    text(20, 140, ":;<=>?@ [\\]^_ the quick brown fox", 4, 0x101010, 1);
    disc(100, 260, 40, 0xE63946, 1); ring(250, 260, 40, 8, 0x2E6BFF, 1);
    capsule(350, 220, 500, 300, 10, 0x1F9E6B, 1);
}

#ifdef GAME_DEBUG
#include <stdio.h>
void game_debug_dump_bad(void)
{
    for (int i = 0; i < MAX_F; i++) {
        if (!F[i].used) continue;
        for (int k = 0; k < NP; k++) {
            float x = F[i].p[k].x, y = F[i].p[k].y;
            if (!(x == x) || !(y == y) || x < 0 || x > GW || y < -700 || y > GH) {
                Fighter *f = &F[i];
                fprintf(stderr, "BAD fighter %d pt %d (%f,%f) prev(%f,%f) scale=%.2f dead=%d stand=%.2f stun=%.2f flop=%.2f atk=%d hp=%.1f\n",
                        i, k, x, y, F[i].p[k].px, F[i].p[k].py, f->scale, f->dead, f->stand, f->stun, f->flop, f->atk, f->hp);
                return;
            }
        }
    }
}
#endif
