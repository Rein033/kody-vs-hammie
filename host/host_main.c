/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test: play the game with scripted buttons (the player side mashes and walks in), check it
 * finishes a match, and write chosen frames as PPM (2x, like the glass). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"

static uint16_t fb[GM_W * GM_H];

static void ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", GM_W * 2, GM_H * 2);
    for ( int y = 0; y < GM_H * 2; y++ )
        for ( int x = 0; x < GM_W * 2; x++ )
        {
            uint16_t c = fb[(y / 2) * GM_W + x / 2];
            unsigned char rgb[3] = { (unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    const int sel_hammie = argc > 2 ? atoi(argv[2]) : 0;
    game_init(42);
    uint32_t rng = 7;
    char path[256];
    int shots[] = { 1, 30, 200, 380, 520, 700, 900, 1300, 2000, 3000, 4500, 6000, 9000 };
    int si = 0, ev_count[16] = { 0 };
    uint32_t prev = 0;
    for ( int step = 1; step < 40000; step++ )
    {
        gm_input_t in = { 0, 0 };
        if ( step == 20 ) in.pressed = GM_TOP;                     /* title -> select */
        if ( step == 40 && sel_hammie ) in.pressed = GM_RIGHT;     /* choose Hammie */
        if ( step == 60 ) in.pressed = GM_TOP;                     /* fight */
        if ( step > 60 )
        {
            rng = rng * 1103515245u + 12345u;
            uint32_t r = (rng >> 16) % 100;
            in.held = GM_RIGHT;
            if ( r < 6 ) in.pressed = GM_TOP;
            else if ( r < 8 ) { in.pressed = GM_TOP; in.held |= GM_DOWN; }
            else if ( r < 9 ) in.pressed = GM_UP;
            else if ( r < 10 ) in.pressed = GM_SPECIAL;
            else if ( r < 18 ) in.held = GM_DOWN;
        }
        (void)prev;
        uint32_t ev = game_step(&in);
        for ( int b = 0; b < 10; b++ ) if ( ev & (1u << b) ) ev_count[b]++;
        if ( si < (int)(sizeof(shots) / sizeof(shots[0])) && step == shots[si] )
        {
            game_draw(fb);
            snprintf(path, sizeof(path), "%s/f%05d.ppm", out, step);
            ppm(path);
            printf("%5d %s\n", step, game_summary());
            si++;
        }
        if ( ev & GM_EV_WIN )
        {
            for ( int k = 0; k < 60; k++ ) game_step(&(gm_input_t){ 0, 0 });
            game_draw(fb);
            snprintf(path, sizeof(path), "%s/result.ppm", out);
            ppm(path);
            printf("WIN at step %d: %s\n", step, game_summary());
            break;
        }
    }
    printf("events hit=%d block=%d ko=%d special=%d powerup=%d round=%d\n", ev_count[0], ev_count[1], ev_count[2], ev_count[3], ev_count[4], ev_count[5]);
    return 0;
}
