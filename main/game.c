/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file game.c
 * @brief Kody vs Hammie: title, fighter select, best-of-three fight against the CPU, result.
 *
 * The fight: walk, jump, block (DOWN), punch (A), kick (DOWN + A, or A in the air), and a special
 * (B tapped, costs half the energy bar): Kody throws his laptop, Hammie fires the wave off his
 * scope. Power-ups drop into the arena: a battery (energy), coffee (speed), a soldering iron
 * (harder hits) and a heart (health).
 */
#include "game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "font.h"
#include "sprites_gen.h"

#define RGB(rgb) ((uint16_t)((((rgb) >> 8) & 0xF800) | (((rgb) >> 5) & 0x07E0) | (((rgb) >> 3) & 0x001F)))

#define C_INK    RGB(0x191919)
#define C_WHITE  RGB(0xFFFFFF)
#define C_YELLOW RGB(0xFFC21A)
#define C_RED    RGB(0xE5383B)
#define C_GREEN  RGB(0x5EE08A)
#define C_BLUE   RGB(0x028AC2)
#define C_ORANGE RGB(0xF6861A)
#define C_GRAY   RGB(0x707070)
#define C_DARK   RGB(0x2A2F3A)
#define C_LIME   RGB(0xB6E848)

#define FLOOR      226          /* the feet's row */
#define X_MIN      18.0f
#define X_MAX      187.0f
#define GRAVITY    0.30f
#define JUMP_V     5.6f
#define WALK_V     1.25f
#define ROUND_S    60
#define WINS_TO    2

/* ---------------------------------------------------------------- random */
static uint32_t s_rng = 1;
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
static float rndf(void) { return (float)(rnd() & 0xFFFF) / 65536.0f; }

/* ---------------------------------------------------------------- the fighters */
typedef enum { CH_KODY = 0, CH_HAMMIE, CH_COUNT } ch_t;
static const char *const CH_NAME[CH_COUNT] = { "KODY", "HAMMIE" };

typedef enum { ST_IDLE, ST_WALK, ST_JUMP, ST_BLOCK, ST_PUNCH, ST_KICK, ST_SPECIAL, ST_HIT, ST_KO, ST_WIN } st_t;

typedef struct {
    float  startup, active, total, reach, dmg, stun, push;
} move_t;

/* frames at 60/s */
static const move_t M_PUNCH = { 5, 5, 18, 40, 6, 14, 2.0f };
static const move_t M_KICK  = { 8, 6, 26, 46, 9, 18, 3.5f };

typedef struct {
    ch_t    ch;
    bool    cpu;
    float   x, y, vx, vy;    /* y: height above the floor */
    int8_t  face;            /* +1 right, -1 left */
    st_t    st;
    int     t;               /* frames in the state */
    int     hp, energy;
    bool    hit_done;        /* this move already landed */
    bool    air_kick;
    int     speed_t, power_t;  /* frames left of coffee and soldering iron */
    int     flash;           /* frames of white after a hit */
    int     wins;
    /* the CPU's mind */
    int     think;           /* frames to the next decision */
    uint32_t want;           /* GM_* it holds */
    int     want_t;
} fighter_t;

/* ---------------------------------------------------------------- the rest of the arena */
typedef struct { bool on; float x, y, vx; int owner; int t; } shot_t;
typedef enum { PU_BATTERY = 0, PU_COFFEE, PU_IRON, PU_HEART, PU_COUNT } pu_t;
typedef struct { bool on; pu_t kind; float x, y; int t; } powerup_t;
typedef struct { bool on; float x, y, vx, vy; int t; uint16_t c; } spark_t;

#define MAX_SHOTS  4
#define MAX_SPARKS 24

typedef enum { SC_TITLE, SC_SELECT, SC_FIGHT, SC_RESULT } screen_t;
typedef enum { RP_INTRO, RP_FIGHT, RP_KO, RP_OVER } phase_t;

static struct {
    screen_t  screen;
    int       sel;            /* the player's fighter on the select screen */
    uint32_t  frame;
    fighter_t f[2];           /* 0 = player, 1 = CPU */
    shot_t    shot[MAX_SHOTS];
    powerup_t pu;
    int       pu_next;        /* frames to the next drop */
    spark_t   spark[MAX_SPARKS];
    phase_t   phase;
    int       phase_t;
    int       round;
    int       clock;          /* frames left in the round */
    int       shake;
    int       winner;         /* of the round / match: 0, 1, or -1 for a draw */
    const char *banner;       /* the text across the arena */
    char      line[96];
} G;

/* ---------------------------------------------------------------- drawing helpers */
static void fill(uint16_t *fb, int x0, int y0, int w, int h, uint16_t c)
{
    for ( int y = y0; y < y0 + h; y++ )
    {
        if ( (0 > y) || (GM_H <= y) ) continue;
        for ( int x = x0; x < x0 + w; x++ )
        {
            if ( (0 <= x) && (GM_W > x) ) fb[y * GM_W + x] = c;
        }
    }
}

static void frame_rect(uint16_t *fb, int x0, int y0, int w, int h, uint16_t c)
{
    fill(fb, x0, y0, w, 1, c);
    fill(fb, x0, y0 + h - 1, w, 1, c);
    fill(fb, x0, y0, 1, h, c);
    fill(fb, x0 + w - 1, y0, 1, h, c);
}

/** @brief A sprite with its feet's centre at (cx, foot); mirrored for face < 0; scale 1 or 2. */
static void blit(uint16_t *fb, const uint16_t *px, int w, int h, int cx, int foot_row, int foot, int face, int scale,
                 int flash)
{
    const int x0 = cx - (w / 2) * scale;
    const int y0 = foot - foot_row * scale;
    for ( int y = 0; y < h * scale; y++ )
    {
        const int yy = y0 + y;
        if ( (0 > yy) || (GM_H <= yy) ) continue;
        const uint16_t *row = px + (y / scale) * w;
        for ( int x = 0; x < w * scale; x++ )
        {
            const int xx = x0 + x;
            if ( (0 > xx) || (GM_W <= xx) ) continue;
            int sx = x / scale;
            if ( 0 > face ) sx = w - 1 - sx;
            uint16_t c = row[sx];
            if ( SPR_KEY == c ) continue;
            if ( flash ) c = (uint16_t)(c | 0x8410);   /* toward white */
            fb[yy * GM_W + xx] = c;
        }
    }
}

static void text_c(uint16_t *fb, int cx, int y, const char *t, int scale, uint16_t c)
{
    /* a line too wide for the glass comes down a size */
    while ( (1 < scale) && (GM_W - 6 < font_width(t, scale)) )
    {
        scale--;
    }
    font_draw_centred(fb, GM_W, GM_H, cx, y, t, scale, c, C_INK);
}

/* ---------------------------------------------------------------- the arena's picture */
static void draw_arena(uint16_t *fb, int sx)
{
    /* the wall: a workshop at night, darker toward the top */
    for ( int y = 0; y < FLOOR; y++ )
    {
        const int k = y * 40 / FLOOR;
        const uint16_t c = (uint16_t)(((4 + k / 3) << 11) | ((10 + k / 2) << 5) | (14 + k / 3));
        for ( int x = 0; x < GM_W; x++ ) fb[y * GM_W + x] = c;
    }
    /* a pegboard with tools, and a bench, behind the fighters */
    fill(fb, 20 + sx, 70, 60, 46, RGB(0x2B3445));
    for ( int y = 74; y < 114; y += 6 )
    {
        for ( int x = 24 + sx; x < 78 + sx; x += 6 ) fill(fb, x, y, 1, 1, RGB(0x46546C));
    }
    fill(fb, 30 + sx, 78, 3, 24, RGB(0xB0B0B0));
    fill(fb, 44 + sx, 76, 2, 28, RGB(0xFFC21A));
    fill(fb, 58 + sx, 80, 6, 4, RGB(0x909090));
    fill(fb, 60 + sx, 84, 2, 18, RGB(0x909090));
    fill(fb, 120 + sx, 150, 70, 6, RGB(0x6B4A2E));
    fill(fb, 124 + sx, 156, 4, 70, RGB(0x4E3520));
    fill(fb, 182 + sx, 156, 4, 70, RGB(0x4E3520));
    fill(fb, 132 + sx, 138, 22, 12, RGB(0x333A44));
    fill(fb, 135 + sx, 140, 16, 7, RGB(0x36863C));
    fill(fb, 160 + sx, 142, 10, 8, RGB(0xF6861A));
    /* the floor */
    for ( int y = FLOOR; y < GM_H; y++ )
    {
        const uint16_t c = (0 == ((y - FLOOR) / 6) % 2) ? RGB(0x3B3F4A) : RGB(0x343843);
        for ( int x = 0; x < GM_W; x++ ) fb[y * GM_W + x] = c;
    }
    fill(fb, 0, FLOOR, GM_W, 1, RGB(0x5A6070));
}

/* ---------------------------------------------------------------- the fight */
static int pose_of(const fighter_t *f)
{
    switch ( f->st )
    {
    case ST_IDLE: return ((G.frame / 24) % 2) ? POSE_IDLE1 : POSE_IDLE0;
    case ST_WALK: return ((G.frame / 9) % 2) ? POSE_WALK1 : POSE_WALK0;
    case ST_JUMP: return POSE_JUMP;
    case ST_BLOCK: return POSE_BLOCK;
    case ST_PUNCH: return (f->t < (int)(M_PUNCH.startup + M_PUNCH.active)) ? POSE_PUNCH : POSE_IDLE0;
    case ST_KICK: return (f->t < (int)(M_KICK.startup + M_KICK.active)) ? POSE_KICK : POSE_CROUCH;
    case ST_SPECIAL: return POSE_SPECIAL;
    case ST_HIT: return POSE_HIT;
    case ST_KO: return POSE_KO;
    case ST_WIN: return POSE_WIN;
    default: return POSE_IDLE0;
    }
}

static void spark(float x, float y, uint16_t c, int n)
{
    for ( int k = 0; k < MAX_SPARKS && n; k++ )
    {
        spark_t *s = &G.spark[k];
        if ( s->on ) continue;
        s->on = true;
        s->x = x;
        s->y = y;
        const float a = rndf() * 6.2832f;
        const float v = 0.8f + rndf() * 1.8f;
        s->vx = cosf(a) * v;
        s->vy = sinf(a) * v - 0.6f;
        s->t = 14 + (int)(rnd() % 10);
        s->c = c;
        n--;
    }
}

static void fighter_reset(fighter_t *f, ch_t ch, bool cpu, float x, int face)
{
    const int wins = f->wins;
    memset(f, 0, sizeof(*f));
    f->ch = ch;
    f->cpu = cpu;
    f->x = x;
    f->face = (int8_t)face;
    f->hp = 100;
    f->energy = 20;
    f->wins = wins;
    f->think = 30;
}

static void set_state(fighter_t *f, st_t st)
{
    f->st = st;
    f->t = 0;
    f->hit_done = false;
}

static bool busy(const fighter_t *f)
{
    return (ST_PUNCH == f->st) || (ST_KICK == f->st) || (ST_SPECIAL == f->st) || (ST_HIT == f->st) ||
           (ST_KO == f->st) || (ST_WIN == f->st);
}

static int scaled(const fighter_t *f, float dmg)
{
    float d = dmg;
    if ( f->power_t ) d *= 1.5f;
    return (int)(d + 0.5f);
}

/** @brief @p a's move lands on @p b (or is blocked). */
static uint32_t land(fighter_t *a, fighter_t *b, int dmg, float stun, float push)
{
    uint32_t ev = 0;
    const bool blocking = (ST_BLOCK == b->st) && (b->face == -a->face) && (0.5f > b->y);
    if ( blocking )
    {
        b->hp -= dmg / 4;
        b->x += (float)a->face * push * 0.6f;
        b->energy += 5;
        spark(b->x - b->face * 10.0f, (float)FLOOR - 40.0f, C_WHITE, 4);
        ev |= GM_EV_BLOCK;
    }
    else
    {
        b->hp -= dmg;
        b->vx = (float)a->face * push;
        b->energy += 4;
        a->energy += 8;
        b->flash = 5;
        set_state(b, ST_HIT);
        b->t = -(int)stun;   /* ST_HIT counts up to 0 */
        spark(b->x - b->face * 8.0f, (float)FLOOR - 40.0f - b->y, C_YELLOW, 8);
        G.shake = 4;
        ev |= GM_EV_HIT;
        if ( !b->cpu ) ev |= GM_EV_PLAYER_HURT;
    }
    if ( 100 < a->energy ) a->energy = 100;
    if ( 100 < b->energy ) b->energy = 100;
    if ( 0 >= b->hp )
    {
        b->hp = 0;
        set_state(b, ST_KO);
        b->vx = (float)a->face * 2.5f;
        b->vy = 3.0f;
        spark(b->x, (float)FLOOR - 40.0f, C_ORANGE, 14);
        ev |= GM_EV_KO;
    }
    return ev;
}

static uint32_t attack_check(fighter_t *a, fighter_t *b, const move_t *m)
{
    if ( a->hit_done ) return 0;
    if ( (a->t < (int)m->startup) || (a->t >= (int)(m->startup + m->active)) ) return 0;
    const float dx = (b->x - a->x) * (float)a->face;
    const float dy = fabsf(a->y - b->y);
    if ( (0.0f < dx) && (m->reach >= dx) && (34.0f > dy) && (ST_KO != b->st) )
    {
        a->hit_done = true;
        return land(a, b, scaled(a, m->dmg), m->stun, m->push);
    }
    return 0;
}

static void shoot(fighter_t *f, int owner)
{
    for ( int k = 0; k < MAX_SHOTS; k++ )
    {
        shot_t *s = &G.shot[k];
        if ( s->on ) continue;
        s->on = true;
        s->owner = owner;
        s->x = f->x + (float)f->face * 30.0f;
        s->y = (float)FLOOR - 34.0f - f->y;
        s->vx = (float)f->face * 3.2f;
        s->t = 0;
        return;
    }
}

/** @brief Turn what one fighter holds into what it does. */
static uint32_t control(fighter_t *f, fighter_t *o, uint32_t held, uint32_t pressed, int idx)
{
    uint32_t ev = 0;
    const float spd = f->speed_t ? 1.6f : 1.0f;
    if ( (ST_HIT == f->st) || (ST_KO == f->st) || (ST_WIN == f->st) ) return 0;
    /* face the other one when free to turn */
    if ( !busy(f) && (0.5f > f->y) ) f->face = (o->x >= f->x) ? 1 : -1;

    const bool ground = (0.5f > f->y) && (ST_JUMP != f->st);
    if ( !busy(f) )
    {
        if ( (pressed & GM_SPECIAL) && (50 <= f->energy) && ground )
        {
            f->energy -= 50;
            set_state(f, ST_SPECIAL);
            f->vx = 0;
            ev |= GM_EV_SPECIAL;
        }
        else if ( pressed & GM_TOP )
        {
            if ( !ground )
            {
                set_state(f, ST_KICK);
                f->air_kick = true;
                f->st = ST_KICK;
            }
            else if ( held & GM_DOWN )
            {
                set_state(f, ST_KICK);
                f->air_kick = false;
                f->vx = 0;
            }
            else
            {
                set_state(f, ST_PUNCH);
                f->vx = 0;
            }
        }
        else if ( ground && (pressed & GM_UP) )
        {
            set_state(f, ST_JUMP);
            f->vy = JUMP_V;
            f->vx = 0;
            if ( held & GM_LEFT ) f->vx = -WALK_V * 1.3f * spd;
            if ( held & GM_RIGHT ) f->vx = WALK_V * 1.3f * spd;
        }
        else if ( ground && (held & GM_DOWN) )
        {
            if ( ST_BLOCK != f->st ) set_state(f, ST_BLOCK);
            f->vx = 0;
        }
        else if ( ground )
        {
            float vx = 0;
            if ( held & GM_LEFT ) vx -= WALK_V * spd;
            if ( held & GM_RIGHT ) vx += WALK_V * spd;
            /* walking backwards is slower */
            if ( (0 > vx * (float)f->face) ) vx *= 0.75f;
            f->vx = vx;
            const st_t want = (0.0f != vx) ? ST_WALK : ST_IDLE;
            if ( want != f->st ) set_state(f, want);
        }
    }
    (void)idx;
    return ev;
}

static uint32_t physics(fighter_t *f, fighter_t *o, int idx)
{
    uint32_t ev = 0;
    f->t++;
    if ( f->flash ) f->flash--;
    if ( f->speed_t ) f->speed_t--;
    if ( f->power_t ) f->power_t--;
    switch ( f->st )
    {
    case ST_PUNCH:
        ev |= attack_check(f, o, &M_PUNCH);
        if ( (int)M_PUNCH.total <= f->t ) set_state(f, ST_IDLE);
        break;
    case ST_KICK:
        ev |= attack_check(f, o, &M_KICK);
        if ( !f->air_kick && ((int)M_KICK.total <= f->t) ) set_state(f, ST_IDLE);
        break;
    case ST_SPECIAL:
        if ( 12 == f->t ) shoot(f, idx);
        if ( 32 <= f->t ) set_state(f, ST_IDLE);
        break;
    case ST_HIT:
        f->vx *= 0.85f;
        if ( 0 <= f->t ) set_state(f, ST_IDLE);
        break;
    case ST_KO:
        f->vx *= 0.95f;
        break;
    default:
        break;
    }
    /* gravity: a jump, an air kick, a KO's fall */
    if ( (0.0f < f->y) || (0.0f < f->vy) )
    {
        f->y += f->vy;
        f->vy -= GRAVITY;
        if ( 0.0f >= f->y )
        {
            f->y = 0;
            f->vy = 0;
            if ( (ST_JUMP == f->st) || ((ST_KICK == f->st) && f->air_kick) ) set_state(f, ST_IDLE);
        }
    }
    f->x += f->vx;
    if ( X_MIN > f->x ) f->x = X_MIN;
    if ( X_MAX < f->x ) f->x = X_MAX;
    return ev;
}

/** @brief The two never stand inside each other. */
static void separate(fighter_t *a, fighter_t *b)
{
    const float d = b->x - a->x;
    const float min = 30.0f;
    if ( (fabsf(d) < min) && (24.0f > fabsf(a->y - b->y)) && (ST_KO != a->st) && (ST_KO != b->st) )
    {
        const float push = (min - fabsf(d)) / 2.0f;
        const float s = (0.0f <= d) ? 1.0f : -1.0f;
        a->x -= s * push;
        b->x += s * push;
        if ( X_MIN > a->x ) { a->x = X_MIN; b->x = a->x + min; }
        if ( X_MAX < b->x ) { b->x = X_MAX; a->x = b->x - min; }
        if ( X_MIN > b->x ) { b->x = X_MIN; a->x = b->x + min; }
        if ( X_MAX < a->x ) { a->x = X_MAX; b->x = a->x - min; }
    }
}

static uint32_t shots_step(void)
{
    uint32_t ev = 0;
    for ( int k = 0; k < MAX_SHOTS; k++ )
    {
        shot_t *s = &G.shot[k];
        if ( !s->on ) continue;
        s->x += s->vx;
        s->t++;
        fighter_t *a = &G.f[s->owner];
        fighter_t *b = &G.f[1 - s->owner];
        if ( (8.0f > s->x) || (GM_W - 8.0f < s->x) )
        {
            s->on = false;
            continue;
        }
        const float by = (float)FLOOR - 34.0f - b->y;
        if ( (16.0f > fabsf(s->x - b->x)) && (30.0f > fabsf(s->y - by)) && (ST_KO != b->st) )
        {
            s->on = false;
            const int8_t face = a->face;
            a->face = (s->vx > 0) ? 1 : -1;
            ev |= land(a, b, scaled(a, 14), 22, 4.0f);
            a->face = face;
        }
    }
    /* two shots that meet cancel out */
    for ( int i = 0; i < MAX_SHOTS; i++ )
    {
        for ( int j = i + 1; j < MAX_SHOTS; j++ )
        {
            shot_t *p = &G.shot[i], *q = &G.shot[j];
            if ( p->on && q->on && (p->owner != q->owner) && (10.0f > fabsf(p->x - q->x)) )
            {
                spark((p->x + q->x) / 2, p->y, C_WHITE, 10);
                p->on = q->on = false;
            }
        }
    }
    return ev;
}

static uint32_t powerup_step(void)
{
    uint32_t ev = 0;
    powerup_t *p = &G.pu;
    if ( !p->on )
    {
        if ( 0 < G.pu_next ) G.pu_next--;
        if ( 0 == G.pu_next )
        {
            p->on = true;
            p->kind = (pu_t)(rnd() % PU_COUNT);
            p->x = 40.0f + rndf() * (GM_W - 80.0f);
            p->y = 28.0f;
            p->t = 0;
        }
        return 0;
    }
    p->t++;
    if ( (float)FLOOR - 8.0f > p->y ) p->y += 1.4f;
    if ( 60 * 7 < p->t )
    {
        p->on = false;
        G.pu_next = 60 * 6 + (int)(rnd() % (60 * 5));
        return 0;
    }
    for ( int i = 0; i < 2; i++ )
    {
        fighter_t *f = &G.f[i];
        if ( (ST_KO == f->st) ) continue;
        const float fy = (float)FLOOR - 30.0f - f->y;
        if ( (18.0f > fabsf(f->x - p->x)) && (34.0f > fabsf(fy - p->y)) )
        {
            switch ( p->kind )
            {
            case PU_BATTERY: f->energy += 50; if ( 100 < f->energy ) f->energy = 100; break;
            case PU_COFFEE: f->speed_t = 60 * 6; break;
            case PU_IRON: f->power_t = 60 * 6; break;
            case PU_HEART: f->hp += 20; if ( 100 < f->hp ) f->hp = 100; break;
            default: break;
            }
            spark(p->x, p->y, C_GREEN, 10);
            p->on = false;
            G.pu_next = 60 * 6 + (int)(rnd() % (60 * 5));
            ev |= GM_EV_POWERUP;
            break;
        }
    }
    return ev;
}

/* ---------------------------------------------------------------- the CPU */
static void cpu_think(fighter_t *f, const fighter_t *o, uint32_t *held, uint32_t *pressed)
{
    *held = 0;
    *pressed = 0;
    if ( f->want_t > 0 )
    {
        f->want_t--;
        *held = f->want;
    }
    if ( 0 < --f->think ) return;
    f->think = 7 + (int)(rnd() % 9);
    const float dx = o->x - f->x;
    const float dist = fabsf(dx);
    const uint32_t toward = (0 < dx) ? GM_RIGHT : GM_LEFT;
    const uint32_t away = (0 < dx) ? GM_LEFT : GM_RIGHT;
    const float r = rndf();
    f->want = 0;
    f->want_t = 0;
    /* the other one swings at it: block, sometimes */
    if ( ((ST_PUNCH == o->st) || (ST_KICK == o->st)) && (55.0f > dist) && (0.45f > r) )
    {
        f->want = GM_DOWN;
        f->want_t = 22;
        *held = GM_DOWN;
        return;
    }
    /* a shot coming: jump it */
    for ( int k = 0; k < MAX_SHOTS; k++ )
    {
        const shot_t *s = &G.shot[k];
        if ( s->on && (&G.f[s->owner] == o) && (70.0f > fabsf(s->x - f->x)) && (0.6f > r) )
        {
            *pressed = GM_UP;
            return;
        }
    }
    /* a power-up nearer to it than to the other one */
    if ( G.pu.on && (fabsf(G.pu.x - f->x) < fabsf(G.pu.x - o->x)) && (0.7f > r) )
    {
        f->want = (G.pu.x > f->x) ? GM_RIGHT : GM_LEFT;
        f->want_t = 18;
        *held = f->want;
        return;
    }
    if ( 70.0f < dist )
    {
        if ( (50 <= f->energy) && (0.35f > r) )
        {
            *pressed = GM_SPECIAL;
            return;
        }
        f->want = toward;
        f->want_t = 14;
        *held = toward;
        return;
    }
    if ( 46.0f < dist )
    {
        if ( 0.15f > r ) { *pressed = GM_UP; *held = toward; return; }
        f->want = toward;
        f->want_t = 10;
        *held = toward;
        return;
    }
    /* close */
    if ( 0.5f > r ) { *pressed = GM_TOP; return; }
    if ( 0.75f > r ) { *pressed = GM_TOP; *held = GM_DOWN; return; }
    if ( 0.85f > r ) { f->want = away; f->want_t = 12; *held = away; return; }
    if ( 0.92f > r ) { *pressed = GM_UP; *held = toward; return; }
    f->want = GM_DOWN;
    f->want_t = 16;
    *held = GM_DOWN;
}

/* ---------------------------------------------------------------- rounds */
static void round_start(void)
{
    const ch_t pc = (ch_t)G.sel;
    const ch_t cc = (ch_t)(1 - G.sel);
    fighter_reset(&G.f[0], pc, false, 60.0f, 1);
    fighter_reset(&G.f[1], cc, true, 145.0f, -1);
    memset(G.shot, 0, sizeof(G.shot));
    memset(G.spark, 0, sizeof(G.spark));
    G.pu.on = false;
    G.pu_next = 60 * 5;
    G.round++;
    G.clock = ROUND_S * 60;
    G.phase = RP_INTRO;
    G.phase_t = 0;
}

static void match_start(void)
{
    G.f[0].wins = G.f[1].wins = 0;
    G.round = 0;
    G.screen = SC_FIGHT;
    round_start();
}

static uint32_t fight_step(const gm_input_t *in)
{
    uint32_t ev = 0;
    fighter_t *p = &G.f[0], *c = &G.f[1];
    G.phase_t++;
    switch ( G.phase )
    {
    case RP_INTRO:
        if ( 1 == G.phase_t ) ev |= GM_EV_ROUND;
        if ( 70 == G.phase_t ) ev |= GM_EV_FIGHT;
        if ( 100 <= G.phase_t ) { G.phase = RP_FIGHT; G.phase_t = 0; }
        break;
    case RP_FIGHT: {
        ev |= control(p, c, in->held, in->pressed, 0);
        uint32_t ch, cp;
        cpu_think(c, p, &ch, &cp);
        ev |= control(c, p, ch, cp, 1);
        if ( 0 < G.clock ) G.clock--;
        break;
    }
    default:
        break;
    }
    ev |= physics(p, c, 0);
    ev |= physics(c, p, 1);
    separate(p, c);
    if ( RP_FIGHT == G.phase )
    {
        ev |= shots_step();
        ev |= powerup_step();
        const bool ko = (ST_KO == p->st) || (ST_KO == c->st);
        if ( ko || (0 == G.clock) )
        {
            G.winner = -1;
            if ( p->hp > c->hp ) G.winner = 0;
            if ( c->hp > p->hp ) G.winner = 1;
            if ( 0 <= G.winner ) G.f[G.winner].wins++;
            G.phase = RP_KO;
            G.phase_t = 0;
        }
    }
    else if ( RP_KO == G.phase )
    {
        if ( (60 == G.phase_t) && (0 <= G.winner) )
        {
            fighter_t *w = &G.f[G.winner];
            if ( ST_KO != w->st ) set_state(w, ST_WIN);
            w->vx = 0;
        }
        if ( 170 <= G.phase_t )
        {
            if ( (WINS_TO <= p->wins) || (WINS_TO <= c->wins) || (5 <= G.round) )
            {
                G.screen = SC_RESULT;
                G.phase_t = 0;
                G.winner = (p->wins > c->wins) ? 0 : ((c->wins > p->wins) ? 1 : -1);
                ev |= GM_EV_WIN;
            }
            else
            {
                round_start();
            }
        }
    }
    for ( int k = 0; k < MAX_SPARKS; k++ )
    {
        spark_t *s = &G.spark[k];
        if ( !s->on ) continue;
        s->x += s->vx;
        s->y += s->vy;
        s->vy += 0.12f;
        if ( 0 >= --s->t ) s->on = false;
    }
    if ( G.shake ) G.shake--;
    return ev;
}

/* ---------------------------------------------------------------- the game */
void game_init(uint32_t seed)
{
    memset(&G, 0, sizeof(G));
    s_rng = seed ? seed : 0x1234567u;
    G.screen = SC_TITLE;
}

uint32_t game_step(const gm_input_t *in)
{
    uint32_t ev = 0;
    G.frame++;
    rnd();
    switch ( G.screen )
    {
    case SC_TITLE:
        if ( in->pressed & (GM_TOP | GM_SPECIAL) )
        {
            G.screen = SC_SELECT;
            ev |= GM_EV_MENU;
        }
        break;
    case SC_SELECT:
        if ( in->pressed & (GM_LEFT | GM_RIGHT) )
        {
            G.sel = 1 - G.sel;
            ev |= GM_EV_MENU;
        }
        if ( in->pressed & GM_TOP )
        {
            match_start();
            ev |= GM_EV_MENU;
        }
        else if ( in->pressed & GM_SPECIAL )
        {
            G.screen = SC_TITLE;
        }
        break;
    case SC_FIGHT:
        ev |= fight_step(in);
        break;
    case SC_RESULT:
        G.phase_t++;
        G.f[0].t++;
        G.f[1].t++;
        if ( 40 < G.phase_t )
        {
            if ( in->pressed & GM_TOP ) { match_start(); ev |= GM_EV_MENU; }
            else if ( in->pressed & GM_SPECIAL ) { G.screen = SC_SELECT; ev |= GM_EV_MENU; }
        }
        break;
    default:
        break;
    }
    return ev;
}

/* ---------------------------------------------------------------- painting */
static void bar(uint16_t *fb, int x, int y, int w, int h, int val, int max, uint16_t c, bool right)
{
    fill(fb, x - 1, y - 1, w + 2, h + 2, C_INK);
    fill(fb, x, y, w, h, RGB(0x50262A));
    int v = val * w / max;
    if ( 0 > v ) v = 0;
    if ( right ) fill(fb, x + w - v, y, v, h, c);
    else fill(fb, x, y, v, h, c);
}

static void draw_hud(uint16_t *fb)
{
    const fighter_t *p = &G.f[0], *c = &G.f[1];
    bar(fb, 6, 8, 80, 7, p->hp, 100, (30 < p->hp) ? C_YELLOW : C_RED, true);
    bar(fb, GM_W - 86, 8, 80, 7, c->hp, 100, (30 < c->hp) ? C_YELLOW : C_RED, false);
    bar(fb, 6, 19, 50, 3, p->energy, 100, (50 <= p->energy) ? C_LIME : C_BLUE, true);
    bar(fb, GM_W - 56, 19, 50, 3, c->energy, 100, (50 <= c->energy) ? C_LIME : C_BLUE, false);
    font_draw(fb, GM_W, GM_H, 6, 25, CH_NAME[p->ch], 1, C_WHITE, C_INK);
    font_draw(fb, GM_W, GM_H, GM_W - 6 - font_width(CH_NAME[c->ch], 1), 25, CH_NAME[c->ch], 1, C_WHITE, C_INK);
    /* the wins, as dots under the names */
    for ( int k = 0; k < WINS_TO; k++ )
    {
        fill(fb, 6 + k * 7, 35, 5, 5, (k < p->wins) ? C_YELLOW : C_DARK);
        fill(fb, GM_W - 11 - k * 7, 35, 5, 5, (k < c->wins) ? C_YELLOW : C_DARK);
    }
    char t[16];
    snprintf(t, sizeof(t), "%d", (G.clock + 59) / 60);
    text_c(fb, GM_W / 2, 7, t, 2, C_WHITE);
    /* buffs */
    for ( int i = 0; i < 2; i++ )
    {
        const fighter_t *f = &G.f[i];
        int x = (0 == i) ? 60 : GM_W - 76;
        if ( f->speed_t ) { blit(fb, ICONS[ICON_COFFEE], ICON_N, ICON_N, x + 8, ICON_N, 44, 1, 1, 0); x += 18; }
        if ( f->power_t ) { blit(fb, ICONS[ICON_IRON], ICON_N, ICON_N, x + 8, ICON_N, 44, 1, 1, 0); }
    }
}

static void draw_fighter(uint16_t *fb, const fighter_t *f, int sx)
{
    const uint16_t *const *set = (CH_KODY == f->ch) ? SPR_KODY : SPR_HAMMIE;
    const int foot = FLOOR - (int)f->y;
    /* a shadow on the floor */
    const int sw = 26 - (int)(f->y / 4);
    for ( int x = -sw / 2; x < sw / 2; x++ ) fill(fb, (int)f->x + x + sx, FLOOR + 1, 1, 2, RGB(0x23262E));
    blit(fb, set[pose_of(f)], SPR_W, SPR_H, (int)f->x + sx, SPR_FOOT_Y, foot + 3, f->face, 1, f->flash);
    /* a buff shows as a glint */
    if ( (f->speed_t || f->power_t) && (0 == (G.frame / 6) % 2) )
    {
        fill(fb, (int)f->x + sx - 14, foot - 70, 2, 2, f->power_t ? C_ORANGE : C_WHITE);
        fill(fb, (int)f->x + sx + 13, foot - 50, 2, 2, f->power_t ? C_ORANGE : C_WHITE);
    }
}

static void draw_shots(uint16_t *fb, int sx)
{
    for ( int k = 0; k < MAX_SHOTS; k++ )
    {
        const shot_t *s = &G.shot[k];
        if ( !s->on ) continue;
        const fighter_t *o = &G.f[s->owner];
        const int x = (int)s->x + sx, y = (int)s->y;
        if ( CH_HAMMIE == o->ch )
        {
            /* the scope's trace, flying: a lime sine wave */
            for ( int i = -14; i <= 14; i++ )
            {
                const int yy = y + (int)(6.0f * sinf((float)(i + s->t * 2) * 0.45f));
                fill(fb, x + i, yy - 1, 1, 3, C_INK);
                fill(fb, x + i, yy, 1, 1, C_LIME);
            }
        }
        else
        {
            /* Kody's laptop, spinning */
            const int ph = (s->t / 4) % 4;
            const int w = (0 == ph % 2) ? 16 : 6;
            fill(fb, x - w / 2 - 1, y - 7, w + 2, 12, C_INK);
            fill(fb, x - w / 2, y - 6, w, 10, RGB(0xB8BEC8));
            if ( 0 == ph % 2 ) fill(fb, x - w / 2 + 2, y - 4, w - 4, 5, RGB(0x2E8BC0));
        }
    }
}

static void draw_fight(uint16_t *fb)
{
    int sx = 0;
    if ( G.shake ) sx = (G.shake % 2) ? 2 : -2;
    draw_arena(fb, sx);
    if ( G.pu.on && !((60 * 6 < G.pu.t) && (G.frame / 4) % 2) )
    {
        static const int MAP[PU_COUNT] = { ICON_BATTERY, ICON_COFFEE, ICON_IRON, ICON_HEART };
        blit(fb, ICONS[MAP[G.pu.kind]], ICON_N, ICON_N, (int)G.pu.x + sx, ICON_N, (int)G.pu.y + 8, 1, 1, 0);
    }
    /* the one attacking is drawn in front */
    const int front = (busy(&G.f[1]) && !busy(&G.f[0])) ? 1 : 0;
    draw_fighter(fb, &G.f[1 - front], sx);
    draw_fighter(fb, &G.f[front], sx);
    draw_shots(fb, sx);
    for ( int k = 0; k < MAX_SPARKS; k++ )
    {
        const spark_t *s = &G.spark[k];
        if ( s->on ) fill(fb, (int)s->x + sx, (int)s->y, 2, 2, s->c);
    }
    draw_hud(fb);
    char t[24];
    if ( RP_INTRO == G.phase )
    {
        if ( 70 > G.phase_t )
        {
            snprintf(t, sizeof(t), "ROUND %d", G.round);
            text_c(fb, GM_W / 2, 90, t, 3, C_WHITE);
        }
        else
        {
            text_c(fb, GM_W / 2, 88, "FIGHT!", 4, C_YELLOW);
        }
    }
    else if ( RP_KO == G.phase )
    {
        const bool ko = (ST_KO == G.f[0].st) || (ST_KO == G.f[1].st);
        text_c(fb, GM_W / 2, 84, ko ? "K.O.!" : "TIME!", 4, C_RED);
        if ( 60 < G.phase_t )
        {
            if ( 0 <= G.winner ) snprintf(t, sizeof(t), "%s WINS", CH_NAME[G.f[G.winner].ch]);
            else snprintf(t, sizeof(t), "DRAW");
            text_c(fb, GM_W / 2, 120, t, 2, C_WHITE);
        }
    }
}

static void draw_title(uint16_t *fb)
{
    draw_arena(fb, 0);
    text_c(fb, GM_W / 2, 28, "KODY", 4, C_BLUE);
    text_c(fb, GM_W / 2, 62, "VS", 3, C_WHITE);
    text_c(fb, GM_W / 2, 88, "HAMMIE", 4, C_ORANGE);
    const int b = (int)((G.frame / 20) % 2);
    blit(fb, SPR_KODY[b ? POSE_IDLE1 : POSE_IDLE0], SPR_W, SPR_H, 55, SPR_FOOT_Y, FLOOR + 3, 1, 1, 0);
    blit(fb, SPR_HAMMIE[b ? POSE_IDLE0 : POSE_IDLE1], SPR_W, SPR_H, 150, SPR_FOOT_Y, FLOOR + 3, -1, 1, 0);
    if ( (G.frame / 30) % 2 ) text_c(fb, GM_W / 2, 132, "PRESS A", 2, C_YELLOW);
}

static void draw_select(uint16_t *fb)
{
    for ( int i = 0; i < GM_W * GM_H; i++ ) fb[i] = RGB(0x1C2030);
    text_c(fb, GM_W / 2, 10, "CHOOSE YOUR", 2, C_WHITE);
    text_c(fb, GM_W / 2, 28, "FIGHTER", 2, C_YELLOW);
    for ( int i = 0; i < CH_COUNT; i++ )
    {
        const int cx = (0 == i) ? 52 : 153;
        const bool on = (G.sel == i);
        fill(fb, cx - 48, 52, 96, 150, on ? RGB(0x2E3A58) : RGB(0x22283A));
        frame_rect(fb, cx - 48, 52, 96, 150, on ? C_YELLOW : RGB(0x3A4256));
        if ( on ) frame_rect(fb, cx - 47, 53, 94, 148, C_YELLOW);
        const uint16_t *const *set = (0 == i) ? SPR_KODY : SPR_HAMMIE;
        int pose = POSE_IDLE0;
        if ( on ) pose = ((G.frame / 30) % 3) ? POSE_IDLE1 : POSE_WIN;
        /* the sprite at 2x would be too wide for a card: 1x, raised, with its name */
        blit(fb, set[pose], SPR_W, SPR_H, cx, SPR_FOOT_Y, 170, (0 == i) ? 1 : -1, 1, 0);
        text_c(fb, cx, 180, CH_NAME[i], 2, on ? C_WHITE : C_GRAY);
    }
    const char *spec = (0 == G.sel) ? "SPECIAL: LAPTOP THROW" : "SPECIAL: SCOPE WAVE";
    text_c(fb, GM_W / 2, 210, spec, 1, C_LIME);
    text_c(fb, GM_W / 2, 226, "LEFT RIGHT CHOOSE  A FIGHT", 1, C_WHITE);
}

static void draw_result(uint16_t *fb)
{
    draw_arena(fb, 0);
    const int w = G.winner;
    if ( 0 <= w )
    {
        const fighter_t *f = &G.f[w];
        const uint16_t *const *set = (CH_KODY == f->ch) ? SPR_KODY : SPR_HAMMIE;
        blit(fb, set[POSE_WIN], SPR_W, SPR_H, GM_W / 2, SPR_FOOT_Y, FLOOR + 3, 1, 2, 0);
        char t[24];
        snprintf(t, sizeof(t), "%s WINS!", CH_NAME[f->ch]);
        text_c(fb, GM_W / 2, 20, t, 3, (CH_KODY == f->ch) ? C_BLUE : C_ORANGE);
        text_c(fb, GM_W / 2, 48, (0 == w) ? "YOU WIN" : "CPU WINS", 2, C_WHITE);
    }
    else
    {
        text_c(fb, GM_W / 2, 30, "DRAW", 4, C_WHITE);
    }
    if ( 40 < G.phase_t ) text_c(fb, GM_W / 2, 236, "A REMATCH   B FIGHTERS", 1, C_YELLOW);
}

void game_draw(uint16_t *fb)
{
    switch ( G.screen )
    {
    case SC_TITLE: draw_title(fb); break;
    case SC_SELECT: draw_select(fb); break;
    case SC_FIGHT: draw_fight(fb); break;
    case SC_RESULT: draw_result(fb); break;
    default: break;
    }
}

const char *game_summary(void)
{
    static const char *const SC[] = { "title", "select", "fight", "result" };
    snprintf(G.line, sizeof(G.line), "screen=%s round=%d p_hp=%d cpu_hp=%d wins=%d-%d clock=%d", SC[G.screen], G.round,
             G.f[0].hp, G.f[1].hp, G.f[0].wins, G.f[1].wins, G.clock / 60);
    return G.line;
}
