/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file app_main.c
 * @brief Kody vs Hammie: a one-on-one fight between the Dot's two mascots.
 *
 * kode_init() brings kodeOS up; one Active Object owns the fight and the glass (fight_ao.c).
 * Any reset returns to kodeOS (CONFIG_KODEOS_BOOT_ROLE_APP).
 */
#include "kode.h"

#include "fight_ao.h"

KODE_APP_MANIFEST("Kody vs Hammie", "games");

void app_main(void)
{
    kode_init();
    FightAo_ctor();
    FightAo_start();
}
