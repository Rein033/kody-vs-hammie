/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file fight_ao.h
 * @brief The Active Object that runs the fight: one frame per event, painted and flipped.
 */
#ifndef FIGHT_AO_H_
#define FIGHT_AO_H_

#include "kode_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The fight's Active Object. */
extern QActive * const AO_Fight;

/** @brief Construct it. */
void FightAo_ctor(void);

/** @brief Start it. */
void FightAo_start(void);

#ifdef __cplusplus
}
#endif

#endif /* FIGHT_AO_H_ */
