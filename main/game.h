/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file game.h
 * @brief Kody vs Hammie: the screens, the fight, the CPU fighter and the power-ups.
 *
 * Pure C: no SDK. The AO feeds it the buttons and a clock, and it paints a
 * GM_W x GM_H RGB565 frame (half the glass; the AO doubles it). Steps are a
 * fixed 1/60 s.
 */
#ifndef GAME_H_
#define GAME_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GM_W 205
#define GM_H 251

/** @brief The buttons, as bits of gm_input_t.held. */
enum {
    GM_UP = 1u << 0, GM_DOWN = 1u << 1, GM_LEFT = 1u << 2, GM_RIGHT = 1u << 3,
    GM_TOP = 1u << 4,       /**< A: punch, confirm */
    GM_SPECIAL = 1u << 5,   /**< BOT tapped: the special, back */
};

/** @brief What the game hears each step. */
typedef struct {
    uint32_t held;     /**< GM_* down now */
    uint32_t pressed;  /**< GM_* that went down since the last step (GM_SPECIAL: tapped) */
} gm_input_t;

/** @brief What happened in a step, for sound and the vibration motor. */
enum {
    GM_EV_HIT = 1u << 0, GM_EV_BLOCK = 1u << 1, GM_EV_KO = 1u << 2, GM_EV_SPECIAL = 1u << 3,
    GM_EV_POWERUP = 1u << 4, GM_EV_ROUND = 1u << 5, GM_EV_FIGHT = 1u << 6, GM_EV_WIN = 1u << 7,
    GM_EV_MENU = 1u << 8, GM_EV_PLAYER_HURT = 1u << 9,
};

/** @brief Set the game up at the title. */
void game_init(uint32_t seed);

/** @brief One 1/60 s step. @return GM_EV_* bits for what happened. */
uint32_t game_step(const gm_input_t *in);

/** @brief Paint the frame. */
void game_draw(uint16_t *fb);

/** @brief A line about the state, for the log. */
const char *game_summary(void);

#ifdef __cplusplus
}
#endif

#endif /* GAME_H_ */
