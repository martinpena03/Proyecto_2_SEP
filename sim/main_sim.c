/*
 * main_sim.c - Simulador del render en el PC: guarda cada pantalla como .ppm.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §12.2).
 *
 * Recorre el juego con la máquina de estados real (eventos inyectados y los stubs de
 * tests/stubs.c) y, en los momentos de interés, escribe el framebuffer en
 * <carpeta>/<nombre>.ppm. Sirve para comparar con el mockup de SepHero_BD.pdf página 4
 * y para las figuras del informe, sin la placa.
 *
 * Uso:  make -C tests sim     (deja las imágenes en sim/out/)
 *       python tools/ppm2png.py sim/out/07_play_oscuro.ppm --scale 3
 */
#include <stdio.h>
#include <string.h>
#include "game_fsm.h"
#include "stubs.h"
#include "fb.h"

static GameCtx ctx;
static const char *outdir = ".";

static void frame(UiEvent ev)
{
    stub_advance(30);
    if (stub_song_running && ctx.gs.star_ms) ctx.gs.star_ms = (ctx.gs.star_ms > 30u) ? ctx.gs.star_ms - 30u : 0u;
    fsm_step(&ctx, ev);
    render_frame(&ctx);
}

static void shot(const char *name)
{
    char path[260];
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    if (fb_write_ppm(path) != 0) printf("no se pudo escribir %s\n", path);
    else                         printf("%s\n", path);
}

/* Juega con golpes perfectos hasta 'until' ms de canción; falla 1 de cada 'miss_every'. */
static void play_until(u32 until, u16 miss_every)
{
    static u16 next;
    if (stub_song_ms < 100u) next = 0;
    while (ctx.st == ST_PLAY && stub_song_ms < until) {
        while (next < ctx.song.n && ctx.song.chart[next].t_ms <= stub_song_ms + 30u) {
            if (miss_every == 0u || (next % miss_every) != 0u) {
                stub_push_hit((u8)ctx.song.chart[next].lane, ctx.song.chart[next].t_ms);
            }
            next++;
        }
        frame(UI_NONE);
    }
}

int main(int argc, char **argv)
{
    int i;
    if (argc > 1) outdir = argv[1];
    stub_reset();
    strcpy(stub_sd_records, "MPZ 18400\nABC 12100\nSEP 6400\n");
    memset(&ctx, 0, sizeof ctx);
    debcfg_default(&ctx.cfg);
    buzzer_init(&ctx.bz, 50);
    fb_clear(0);
    fsm_start(&ctx);

    frame(UI_NONE);                         shot("01_init");
    for (i = 0; i < 51; i++) frame(UI_NONE);
    frame(UI_DOWN);                         shot("02_menu");
    frame(UI_OK);                           shot("03_records");
    frame(UI_BACK);  frame(UI_DOWN);  frame(UI_DOWN);  frame(UI_OK);   shot("04_config");
    frame(UI_BACK);  frame(UI_OK);          shot("05_level");

    /* Juego, tema oscuro. */
    frame(UI_OK);
    play_until(1400, 0);                    shot("06_play_cuenta");
    stub_crowd = 60;
    play_until(9300, 0);                    shot("07_play_oscuro");
    frame(UI_BACK);                         shot("08_pausa");
    frame(UI_BACK);
    /* Star power. */
    play_until(9600, 0);
    frame(UI_SHAKE);
    stub_crowd = 85;
    play_until(15300, 0);                   shot("09_play_star_power");
    /* Tema claro con rock meter bajo. */
    stub_theme = THEME_LIGHT;
    stub_crowd = 30;
    play_until(31000, 2);                   shot("10_play_claro_rock_bajo");
    stub_theme = THEME_DARK;
    play_until(0xFFFFFFFFu, 0);
    frame(UI_NONE);  frame(UI_UP);  frame(UI_OK);  frame(UI_UP);  frame(UI_UP);
    for (i = 0; i < 6; i++) frame(UI_NONE);
                                            shot("11_gameover_iniciales");
    frame(UI_OK);  frame(UI_OK);            shot("12_gameover_record");
    printf("fuera de pantalla: %lu (debe ser 0)\n", (unsigned long)fb_oob);
    return fb_oob != 0;
}
