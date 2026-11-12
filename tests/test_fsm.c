/*
 * test_fsm.c - Tests de la máquina de estados (manual §9.2), versión avance.
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio.
 */
#include <string.h>
#include "test.h"
#include "state_machine.h"

static GameCtx ctx;

/* Un frame: 30 ms de reloj, el star power se descuenta como lo hace timer0. */
static void frame(UiEvent ev)
{
    stub_advance(30);
    if (stub_song_running && ctx.gs.star_ms) ctx.gs.star_ms = (ctx.gs.star_ms > 30u) ? ctx.gs.star_ms - 30u : 0u;
    fsm_step(&ctx, ev);
}

static void boot(void)
{
    memset(&ctx, 0, sizeof ctx);
    debcfg_default(&ctx.cfg);
    buzzer_init(&ctx.bz, 50);
    fsm_start(&ctx);
}

/* Juega la canción completa con golpes perfectos (opcionalmente hasta 'until' ms). */
static void autoplay(u32 until)
{
    u16 next = 0;
    while (ctx.st == ST_PLAY && stub_song_ms < until) {
        while (next < ctx.song.n && ctx.song.chart[next].t_ms <= stub_song_ms + 30u) {
            stub_push_hit((u8)ctx.song.chart[next].lane, ctx.song.chart[next].t_ms);
            next++;
        }
        frame(UI_NONE);
    }
}

void test_fsm(void)
{
    int i;
    boot();
    CHECK(ctx.st == ST_INIT && ctx.n_rec == 0 && ctx.sd.mounted == 0);
    for (i = 0; i < 49; i++) frame(UI_NONE);
    CHECK(ctx.st == ST_INIT);
    frame(UI_NONE);
    CHECK(ctx.st == ST_MENU && ctx.bz.playing && ctx.bz.loop);          /* jingle del menú */

    /* Navegación del menú con el joystick (circular). */
    frame(UI_DOWN);  CHECK(ctx.cursor == 1);
    frame(UI_DOWN);  CHECK(ctx.cursor == 2);
    frame(UI_DOWN);  CHECK(ctx.cursor == 0);
    frame(UI_UP);    CHECK(ctx.cursor == 2);

    /* CONFIG: cancelar con S2 y luego configurar por consola. */
    frame(UI_OK);    CHECK(ctx.st == ST_CONFIG && !ctx.bz.playing);
    frame(UI_BACK);  CHECK(ctx.st == ST_MENU && stub_deb_inits == 0);
    frame(UI_UP);    frame(UI_OK);  CHECK(ctx.st == ST_CONFIG);
    stub_uart_feed("15\r\n5\r\n900\r\n6\r\n");
    frame(UI_NONE);
    CHECK(ctx.st == ST_MENU && stub_deb_inits == 1);
    CHECK(stub_deb_cfg.deb_ms == 15 && stub_deb_cfg.btn_mask == 5);     /* llegó al IP */
    CHECK(stub_shake_mg == 900 && ctx.cfg.deb_ms == 15);
    CHECK(strstr(stub_uart_out, "IP: DEB_MS=15 BTN_MASK=0x5 LED_CTRL=0x06 (releidos del hardware)") != NULL);
    CHECK(strstr(stub_uart_out, "ESTADO: MENU") != NULL);

    /* RECORDS y vuelta. */
    frame(UI_DOWN);  frame(UI_OK);   CHECK(ctx.st == ST_RECORDS);
    frame(UI_BACK);  CHECK(ctx.st == ST_MENU);

    /* LEVEL: carga la canción 1, cambia a la 2 y vuelve. */
    frame(UI_OK);    CHECK(ctx.st == ST_LEVEL && ctx.song_ok && ctx.song.n == 62);
    frame(UI_DOWN);  CHECK(ctx.song_sel == 1 && ctx.song.n == 74);
    frame(UI_DOWN);  CHECK(ctx.song_sel == 0 && ctx.song.n == 62);
    frame(UI_BACK);  CHECK(ctx.st == ST_MENU);
    frame(UI_OK);    CHECK(ctx.st == ST_LEVEL);

    /* PLAY: modo juego del joystick, reloj de canción corriendo. */
    frame(UI_OK);
    CHECK(ctx.st == ST_PLAY && stub_play_mode == 1 && stub_song_running && ctx.gs.rock == ROCK_START);
    autoplay(8000);
    CHECK(ctx.gs.hits >= 10 && ctx.gs.misses == 0 && ctx.gs.score == (u32)ctx.gs.hits * PTS_NOTA);
    CHECK(ctx.gs.star_phase == STAR_READY);

    /* PAUSE congela el reloj y silencia; continuar retoma sin perder el estado. */
    {
        u32 t, sc = ctx.gs.score;
        frame(UI_BACK);
        t = stub_song_ms;                                   /* instante en que se pausó */
        CHECK(ctx.st == ST_PAUSE && !stub_song_running && stub_play_mode == 0 && ctx.bz.paused);
        for (i = 0; i < 20; i++) frame(UI_NONE);
        for (i = 0; i < 200; i++) buzzer_tick(&ctx.bz, stub_song_ms);   /* lo que haria timer0 */
        CHECK(stub_song_ms == t && stub_tone_f == 0);
        frame(UI_BACK);
        CHECK(ctx.st == ST_PLAY && stub_song_running && stub_play_mode == 1 && ctx.gs.score == sc);
    }

    /* Sacudida con la barra llena: star power y puntaje doble. */
    frame(UI_SHAKE);
    CHECK(ctx.gs.star_phase == STAR_ACTIVE);
    {
        u32 sc = ctx.gs.score;
        u16 hits = ctx.gs.hits;
        autoplay(stub_song_ms + 3000);
        CHECK(ctx.gs.hits > hits && ctx.gs.score == sc + (u32)(ctx.gs.hits - hits) * 2u * PTS_NOTA);
    }

    /* Pausa -> Reiniciar: partida nueva desde 0. */
    frame(UI_BACK);  frame(UI_DOWN);  frame(UI_OK);
    CHECK(ctx.st == ST_PLAY && stub_song_ms < 100 && ctx.gs.score == 0 && ctx.gs.hits == 0);

    /* Partida perfecta hasta el final => VICTORIA y entra al top 5. */
    autoplay(0xFFFFFFFFu);
    CHECK(ctx.st == ST_GAMEOVER && ctx.result == RESULT_WIN && !stub_song_running);
    CHECK(ctx.gs.hits == 62 && ctx.gs.misses == 0 && ctx.name_entry == 1);

    /* Encore: grito sostenido dentro de los primeros 5 s => +10 %. */
    {
        u32 sc = ctx.gs.score;
        stub_crowd = 90;
        frame(UI_NONE);  frame(UI_NONE);  CHECK(ctx.gs.score == sc);
        frame(UI_NONE);  CHECK(ctx.gs.encore_done && ctx.gs.score == sc + sc / 10u);
        frame(UI_NONE);  CHECK(ctx.gs.score == sc + sc / 10u);          /* una sola vez */
        stub_crowd = 0;
    }

    /* Iniciales "BAZ" con el joystick y S1; se guarda en la "SD". */
    frame(UI_UP);    frame(UI_OK);                  /* B */
    frame(UI_OK);                                   /* A */
    frame(UI_DOWN);  frame(UI_OK);                  /* Z */
    CHECK(ctx.name_entry == 0 && ctx.n_rec == 1 && ctx.rec_pos == 0 && stub_sd_saves == 1);
    CHECK(strcmp(ctx.rec[0].name, "BAZ") == 0 && ctx.rec[0].score == ctx.gs.score);
    CHECK(strstr(stub_sd_records, "BAZ ") != NULL);
    frame(UI_OK);    CHECK(ctx.st == ST_MENU);

    /* Derrota: sin tocar nada el rock meter llega a 0. */
    frame(UI_OK);  frame(UI_OK);  CHECK(ctx.st == ST_PLAY);
    while (ctx.st == ST_PLAY) frame(UI_NONE);
    CHECK(ctx.st == ST_GAMEOVER && ctx.result == RESULT_LOSE && ctx.gs.rock == 0 && ctx.name_entry == 0);
    frame(UI_OK);    CHECK(ctx.st == ST_MENU);

    /* Pausa -> Salir. */
    frame(UI_OK);  frame(UI_OK);  frame(UI_BACK);  frame(UI_UP);  frame(UI_OK);
    CHECK(ctx.st == ST_MENU && stub_play_mode == 0);

    /* Los récords guardados se recuperan al arrancar de nuevo. */
    boot();
    CHECK(ctx.n_rec == 1 && strcmp(ctx.rec[0].name, "BAZ") == 0);
}
