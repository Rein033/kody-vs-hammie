/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file fight_ao.c
 * @brief The fight's Active Object, in the shape of Kody Kart's: a FRAME event posted to itself
 *        steps the game at 60 per second, paints the half-size frame, doubles it into the
 *        panel's back buffer and flips it.
 *
 * Buttons: the held set is read every frame (kode_buttons_read()); presses come from the
 * button events. BOT acts on its release: a tap is the special (or back), a hold of a second
 * goes home to kodeOS.
 */
#include "fight_ao.h"

#include <string.h>

#include "kdrv_buttons.h"
#include "kdrv_display.h"
#include "kaos.h"
#include "kode.h"
#include "kqp_signals.h"

#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "font.h"
#include "game.h"
#include "sound.h"

#define TAG "fight_ao"

#define SC_W 410
#define SC_H 502
_Static_assert((SC_W == GM_W * 2) && (SC_H == GM_H * 2), "the game's frame is half the glass");

#define FLUSH_WAIT_MS 200
#define STEP_MS       (1000.0f / 60.0f)
#define MAX_STEPS     4           /* a slow frame catches up at most this much */
#define HOME_HOLD_MS  1000u
#define LOG_EVERY_MS  10000u

enum FightSignals {
    FIGHT_FRAME_SIG = MAX_SIG,   /**< one frame: step, paint, flip; posted to itself */
};

static const QEvt FRAME_EVT = QEVT_INITIALIZER(FIGHT_FRAME_SIG);

typedef struct {
    QActive   super;
    uint16_t *fb;            /**< GM_W x GM_H, what the game paints */
    bool      in_flight;     /**< a flip was posted and not waited for yet */
    uint32_t  pressed;       /**< GM_* pressed since the last step */
    uint32_t  held;          /**< GM_* down now, from the button events */
    uint32_t  bot_down_ms;   /**< when BOT went down; 0 = up */
    uint32_t  last_ms;
    float     acc;           /**< ms not yet stepped */
    uint32_t  log_at;
    uint32_t  frames;
} FightAo;

static QEvtPtr     l_queue[KQP_AO_QUEUE_LEN];
static StackType_t s_stack[KQP_AO_STACK_BYTES / sizeof(StackType_t)];

static FightAo l_ao;
QActive * const AO_Fight = &l_ao.super;

static QState FightAo_initial(FightAo * const me, void const * const par);
static QState FightAo_running(FightAo * const me, QEvt const * const e);
static QState FightAo_waiting(FightAo * const me, QEvt const * const e);
static QState FightAo_playing(FightAo * const me, QEvt const * const e);

/** @brief Milliseconds since boot (kode.h's clock blocks, so not from an AO). */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ---- the picture ------------------------------------------------------------ */

/** @brief Every game pixel becomes two by two on the glass. */
static void upscale(const uint16_t *src, uint16_t *dst)
{
    for ( int y = 0; GM_H > y; y++ )
    {
        const uint16_t *s = src + y * GM_W;
        uint32_t *d0 = (uint32_t *)(dst + (2 * y) * SC_W);
        for ( int x = 0; GM_W > x; x++ )
        {
            d0[x] = (uint32_t)s[x] * 0x00010001u;
        }
        memcpy(dst + (2 * y + 1) * SC_W, d0, SC_W * sizeof(uint16_t));
    }
}

/** @brief Wait for the last flip, double the frame into the back buffer, flip. */
static void flip(FightAo * const me)
{
    if ( me->in_flight )
    {
        (void)kdrv_display_flush_wait(FLUSH_WAIT_MS);
        me->in_flight = false;
    }
    uint16_t * const back = kdrv_display_back_buffer();
    if ( !back )
    {
        return;
    }
    upscale(me->fb, back);
    FlushEvt * const ev = Q_NEW_X(FlushEvt, 0U, DISPLAY_FLUSH_SIG);
    if ( !ev )
    {
        return;
    }
    ev->x1 = 0;
    ev->y1 = 0;
    ev->x2 = SC_W;
    ev->y2 = SC_H;
    ev->pixels = (uint8_t *)back;
    ev->last = true;
    QACTIVE_POST(AO_Display, &ev->super, &me->super);
    me->in_flight = true;
}

/* ---- sound and feel ----------------------------------------------------------- */

#define PLAY(tune, prio) sound_play((tune), (int)(sizeof(tune) / sizeof((tune)[0]) / 2), (prio))

static void react(uint32_t ev)
{
    static const uint16_t HIT[] = { 220, 40, 150, 50 };
    static const uint16_t BLOCK[] = { 900, 25 };
    static const uint16_t KO[] = { 440, 90, 330, 90, 220, 90, 110, 220 };
    static const uint16_t SPECIAL[] = { 600, 40, 900, 40, 1200, 60 };
    static const uint16_t POWERUP[] = { 784, 50, 1047, 50, 1319, 90 };
    static const uint16_t ROUND[] = { 523, 80 };
    static const uint16_t FIGHT[] = { 784, 70, 1047, 140 };
    static const uint16_t WIN[] = { 523, 100, 659, 100, 784, 100, 1047, 260 };
    static const uint16_t MENU[] = { 1200, 25 };
    if ( ev & GM_EV_KO ) PLAY(KO, SND_HIGH);
    else if ( ev & GM_EV_WIN ) PLAY(WIN, SND_HIGH);
    else if ( ev & GM_EV_FIGHT ) PLAY(FIGHT, SND_MID);
    else if ( ev & GM_EV_ROUND ) PLAY(ROUND, SND_MID);
    else if ( ev & GM_EV_SPECIAL ) PLAY(SPECIAL, SND_MID);
    else if ( ev & GM_EV_POWERUP ) PLAY(POWERUP, SND_MID);
    else if ( ev & GM_EV_HIT ) PLAY(HIT, SND_LOW);
    else if ( ev & GM_EV_BLOCK ) PLAY(BLOCK, SND_LOW);
    else if ( ev & GM_EV_MENU ) PLAY(MENU, SND_LOW);
    if ( ev & GM_EV_KO ) (void)kode_sys_haptic_play_buzz(220, 100);
    else if ( ev & GM_EV_PLAYER_HURT ) (void)kode_sys_haptic_play_buzz(60, 80);
    else if ( ev & GM_EV_HIT ) (void)kode_sys_haptic_play_buzz(25, 45);
}

/* ---- the frame -------------------------------------------------------------- */

static void home_bar(FightAo * const me, uint32_t now)
{
    if ( !me->bot_down_ms || (250u >= now - me->bot_down_ms) )
    {
        return;
    }
    if ( HOME_HOLD_MS <= now - me->bot_down_ms )
    {
        KLOG_I(TAG, "going home");
        (void)kode_sys_exit_to_os();
        return;
    }
    const int w = (int)((now - me->bot_down_ms) * 100u / HOME_HOLD_MS);
    const int x0 = GM_W / 2 - 50, y0 = GM_H / 2 - 4;
    for ( int y = y0 - 1; y0 + 9 > y; y++ )
    {
        for ( int x = x0 - 1; x0 + 101 > x; x++ )
        {
            const bool in = (y0 <= y) && (y0 + 8 > y) && (x0 <= x) && (x0 + 100 > x);
            uint16_t px = 0x2104;
            if ( !in )
            {
                px = 0x18C3;
            }
            else if ( (x - x0) < w )
            {
                px = 0xFD40;
            }
            me->fb[y * GM_W + x] = px;
        }
    }
    font_draw_centred(me->fb, GM_W, GM_H, GM_W / 2, y0 - 14, "HOME", 1, 0xFFFF, 0x18C3);
}

static void frame(FightAo * const me)
{
    const uint32_t now = now_ms();
    me->acc += (float)(now - me->last_ms);
    me->last_ms = now;
    int steps = 0;
    gm_input_t in = { .held = me->held, .pressed = 0 };
    uint32_t ev = 0;
    while ( (STEP_MS <= me->acc) && (MAX_STEPS > steps) )
    {
        in.pressed = me->pressed;
        me->pressed = 0;
        ev |= game_step(&in);
        me->acc -= STEP_MS;
        steps++;
    }
    if ( MAX_STEPS <= steps )
    {
        me->acc = 0;
    }
    react(ev);
    game_draw(me->fb);
    home_bar(me, now);
    sound_step(now);
    flip(me);
    me->frames++;
    if ( LOG_EVERY_MS <= now - me->log_at )
    {
        KLOG_I(TAG, "%u fps, %s", (unsigned)(me->frames * 1000u / LOG_EVERY_MS), game_summary());
        me->frames = 0;
        me->log_at = now;
    }
    vTaskDelay(1);
    QACTIVE_POST(&me->super, &FRAME_EVT, &me->super);
}

/* ---- the AO ----------------------------------------------------------------- */

void FightAo_ctor(void)
{
    FightAo * const me = &l_ao;
    QActive_ctor(&me->super, Q_STATE_CAST(&FightAo_initial));
}

void FightAo_start(void)
{
    QActive_setAttr(AO_Fight, TASK_NAME_ATTR, "fight_ao");
    QACTIVE_START(AO_Fight, KODE_PRIO_USER_GUI, l_queue, Q_DIM(l_queue), s_stack, sizeof(s_stack), (void *)0);
}

static QState FightAo_initial(FightAo * const me, void const * const par)
{
    (void)par;
    me->fb = heap_caps_malloc((size_t)GM_W * GM_H * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    if ( !me->fb )
    {
        me->fb = heap_caps_malloc((size_t)GM_W * GM_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    }
    if ( !me->fb )
    {
        KLOG_E(TAG, "no frame buffer: nothing will show");
    }
    game_init(esp_random());
    QActive_subscribe(&me->super, BUTTON_EVT_SIG);
    QActive_subscribe(&me->super, DISPLAY_STATE_SIG);
    /* The panel was announced from inside kode_init(), before this AO existed. */
    display_state_request();
    return Q_TRAN(&FightAo_waiting);
}

/** @brief Parent of every state: the buttons. */
static QState FightAo_running(FightAo * const me, QEvt const * const e)
{
    switch ( e->sig )
    {
    case BUTTON_EVT_SIG: {
        ButtonEvt const * const be = (ButtonEvt const *)e;
        const uint32_t now = now_ms();
        if ( (uint8_t)KDRV_BTN_BOT == be->id )
        {
            if ( be->pressed )
            {
                uint32_t down = now;
                if ( 0u == now )
                {
                    down = 1;
                }
                me->bot_down_ms = down;
            }
            else
            {
                if ( me->bot_down_ms && (HOME_HOLD_MS > now - me->bot_down_ms) )
                {
                    me->pressed |= GM_SPECIAL;
                }
                me->bot_down_ms = 0;
            }
            return Q_HANDLED();
        }
        static const uint32_t MAP[KDRV_BTN_COUNT] = {
            [KDRV_BTN_UP] = GM_UP, [KDRV_BTN_DOWN] = GM_DOWN, [KDRV_BTN_LEFT] = GM_LEFT,
            [KDRV_BTN_RIGHT] = GM_RIGHT, [KDRV_BTN_TOP] = GM_TOP,
        };
        if ( KDRV_BTN_COUNT > be->id )
        {
            if ( be->pressed )
            {
                me->pressed |= MAP[be->id];
                me->held |= MAP[be->id];
            }
            else
            {
                me->held &= ~MAP[be->id];
            }
        }
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState FightAo_waiting(FightAo * const me, QEvt const * const e)
{
    switch ( e->sig )
    {
    case DISPLAY_STATE_SIG: {
        DisplayStateEvt const * const st = (DisplayStateEvt const *)e;
        if ( me->fb && st->up )
        {
            return Q_TRAN(&FightAo_playing);
        }
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&FightAo_running);
    }
}

static QState FightAo_playing(FightAo * const me, QEvt const * const e)
{
    switch ( e->sig )
    {
    case Q_ENTRY_SIG:
        me->last_ms = me->log_at = now_ms();
        QACTIVE_POST(&me->super, &FRAME_EVT, &me->super);
        return Q_HANDLED();
    case FIGHT_FRAME_SIG:
        frame(me);
        return Q_HANDLED();
    default:
        return Q_SUPER(&FightAo_running);
    }
}
