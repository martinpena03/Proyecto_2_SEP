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
    if (stub_song_running && ctx.game_status.star_ms) ctx.game_status.star_ms = (ctx.game_status.star_ms > 30u) ? ctx.game_status.star_ms - 30u : 0u;
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
    while (ctx.state == ST_PLAY && stub_song_ms < until) {
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
    CHECK(ctx.state == ST_INIT && ctx.n_rec == 0 && ctx.sd.mounted == 0);
    for (i = 0; i < 49; i++) frame(UI_NONE);
    CHECK(ctx.state == ST_INIT);
    frame(UI_NONE);
    CHECK(ctx.state == ST_MENU && ctx.bz.playing && ctx.bz.loop);          /* jingle del menú */

    /* Navegación del menú con el joystick (circular). */
    frame(UI_DOWN);  CHECK(ctx.cursor == 1);
    frame(UI_DOWN);  CHECK(ctx.cursor == 2);
    frame(UI_DOWN);  CHECK(ctx.cursor == 0);
    frame(UI_UP);    CHECK(ctx.cursor == 2);

    /* CONFIG: cancelar con S2 y luego configurar por consola. */
    frame(UI_OK);    CHECK(ctx.state == ST_CONFIG && !ctx.bz.playing);
    frame(UI_BACK);  CHECK(ctx.state == ST_MENU && stub_deb_inits == 0);
    frame(UI_UP);    frame(UI_OK);  CHECK(ctx.state == ST_CONFIG);
    stub_uart_feed("15\r\n5\r\n900\r\n6\r\n");
    frame(UI_NONE);
    CHECK(ctx.state == ST_MENU && stub_deb_inits == 1);
    CHECK(stub_deb_cfg.deb_ms == 15 && stub_deb_cfg.btn_mask == 5);     /* llegó al IP */
    CHECK(stub_shake_mg == 900 && ctx.cfg.deb_ms == 15);
    CHECK(strstr(stub_uart_out, "IP: DEB_MS=15 BTN_MASK=0x5 LED_CTRL=0x06 (releidos del hardware)") != NULL);
    CHECK(strstr(stub_uart_out, "ESTADO: MENU") != NULL);

    /* RECORDS y vuelta. */
    frame(UI_DOWN);  frame(UI_OK);   CHECK(ctx.state == ST_RECORDS);
    frame(UI_BACK);  CHECK(ctx.state == ST_MENU);

    /* LEVEL: parte en el nivel Demo, recorre las canciones (circular) y deja la 1. */
    frame(UI_OK);    CHECK(ctx.state == ST_LEVEL && ctx.song_ok && ctx.select_song == 0 && ctx.song.n == 10);
    CHECK(!ctx.bz.playing);                                 /* avance: niveles sin música */
    CHECK(strstr(stub_uart_out, "[LEVEL]  1/3: ") != NULL);
    frame(UI_DOWN);  CHECK(ctx.select_song == 1 && ctx.song.n == 62);
    frame(UI_DOWN);  CHECK(ctx.select_song == 2 && ctx.song.n == 74);
    frame(UI_DOWN);  CHECK(ctx.select_song == 0 && ctx.song.n == 10);
    frame(UI_DOWN);  CHECK(ctx.select_song == 1 && ctx.song.n == 62);
    frame(UI_BACK);  CHECK(ctx.state == ST_MENU);
    frame(UI_OK);    CHECK(ctx.state == ST_LEVEL);

    /* PLAY: modo juego del joystick, reloj de canción corriendo. */
    frame(UI_OK);
    CHECK(ctx.state == ST_PLAY && stub_play_mode == 1 && stub_song_running && ctx.game_status.rock == ROCK_START);
    CHECK(!ctx.bz.playing && strstr(stub_uart_out, "Juego iniciado\r\n") != NULL);
    frame(UI_NONE);
    CHECK(strstr(stub_uart_out, "[Jugando] quedan 20 s") != NULL);
    CHECK(strstr(stub_uart_out, "[Jugando] quedan 19 s") == NULL);
    autoplay(8000);
    CHECK(ctx.game_status.hits >= 10 && ctx.game_status.misses == 0 && ctx.game_status.score == (u32)ctx.game_status.hits * PTS_NOTA);
    CHECK(ctx.game_status.star_phase == STAR_READY);

    /* PAUSE congela el reloj y silencia; continuar retoma sin perder el estado. */
    {
        u32 t, sc = ctx.game_status.score;
        frame(UI_BACK);
        t = stub_song_ms;                                   /* instante en que se pausó */
        CHECK(ctx.state == ST_PAUSE && !stub_song_running && stub_play_mode == 0 && ctx.bz.paused);
        for (i = 0; i < 20; i++) frame(UI_NONE);
        for (i = 0; i < 200; i++) buzzer_tick(&ctx.bz, stub_song_ms);   /* lo que haria timer0 */
        CHECK(stub_song_ms == t && stub_tone_f == 0);
        frame(UI_BACK);
        CHECK(ctx.state == ST_PLAY && stub_song_running && stub_play_mode == 1 && ctx.game_status.score == sc);
    }

    /* Sacudida con la barra llena: star power y puntaje doble. */
    frame(UI_POWER_UP_ENABLE);
    CHECK(ctx.game_status.star_phase == STAR_ACTIVE);
    {
        u32 sc = ctx.game_status.score;
        u16 hits = ctx.game_status.hits;
        autoplay(stub_song_ms + 3000);
        CHECK(ctx.game_status.hits > hits && ctx.game_status.score == sc + (u32)(ctx.game_status.hits - hits) * 2u * PTS_NOTA);
    }

    /* Pausa -> Reiniciar: partida nueva desde 0. */
    frame(UI_BACK);  frame(UI_DOWN);  frame(UI_OK);
    CHECK(ctx.state == ST_PLAY && stub_song_ms < 100 && ctx.game_status.score == 0 && ctx.game_status.hits == 0);

    /* Partida perfecta: termina por tiempo a los 20 s => VICTORIA y entra al top 5. */
    autoplay(0xFFFFFFFFu);
    CHECK(ctx.state == ST_GAMEOVER && ctx.result == RESULT_WIN && !stub_song_running);
    CHECK(stub_song_ms >= AVANCE_PLAY_MS && stub_song_ms < AVANCE_PLAY_MS + 60u);
    CHECK(ctx.game_status.hits > 20 && ctx.game_status.misses == 0 && ctx.name_entry == 1);
    CHECK(strstr(stub_uart_out, "[Jugando] quedan 1 s") != NULL);
    CHECK(strstr(stub_uart_out, "[Jugando] quedan 0 s") == NULL);
    CHECK(strstr(stub_uart_out, "tiempo cumplido") != NULL);

    /* Encore: grito sostenido dentro de los primeros 5 s => +10 %. */
    {
        u32 sc = ctx.game_status.score;
        stub_crowd = 90;
        frame(UI_NONE);  frame(UI_NONE);  CHECK(ctx.game_status.score == sc);
        frame(UI_NONE);  CHECK(ctx.game_status.encore_done && ctx.game_status.score == sc + sc / 10u);
        frame(UI_NONE);  CHECK(ctx.game_status.score == sc + sc / 10u);          /* una sola vez */
        stub_crowd = 0;
    }

    /* Iniciales "BAZ" con el joystick y S1; se guarda en la "SD". */
    frame(UI_UP);    frame(UI_OK);                  /* B */
    frame(UI_OK);                                   /* A */
    frame(UI_DOWN);  frame(UI_OK);                  /* Z */
    CHECK(ctx.name_entry == 0 && ctx.n_rec == 1 && ctx.rec_pos == 0 && stub_sd_saves == 1);
    CHECK(strcmp(ctx.rec[0].name, "BAZ") == 0 && ctx.rec[0].score == ctx.game_status.score);
    CHECK(strstr(stub_sd_records, "BAZ ") != NULL);
    frame(UI_OK);    CHECK(ctx.state == ST_MENU);

    /* Derrota: sin tocar nada el rock meter llega a 0, pero la partida dura los 20 s. */
    frame(UI_OK);  frame(UI_OK);  CHECK(ctx.state == ST_PLAY);
    while (ctx.state == ST_PLAY) frame(UI_NONE);
    CHECK(stub_song_ms >= AVANCE_PLAY_MS);
    CHECK(ctx.state == ST_GAMEOVER && ctx.result == RESULT_LOSE && ctx.game_status.rock == 0 && ctx.name_entry == 0);
    frame(UI_OK);    CHECK(ctx.state == ST_MENU);

    /* Pausa -> Salir. */
    frame(UI_OK);  frame(UI_OK);  frame(UI_BACK);  frame(UI_UP);  frame(UI_OK);
    CHECK(ctx.state == ST_MENU && stub_play_mode == 0);

    /* Los récords guardados se recuperan al arrancar de nuevo. */
    boot();
    CHECK(ctx.n_rec == 1 && strcmp(ctx.rec[0].name, "BAZ") == 0);
}

/* Nivel Demo: flechas del joystick por consola (+ puntaje), star power con STAR_STREAK
 * aciertos, fin 2 s después de la última flecha, puntaje final y la flecha en el LCD
 * durante DEMO_KEY_MS centrados en cada nota. */
void test_demo(void)
{
    int i;
    boot();
    for (i = 0; i < 50; i++) frame(UI_NONE);
    render_frame(&ctx);
    CHECK(ctx.state == ST_MENU && stub_lcd_count(0x0000) == (u32)LCD_W * LCD_H);   /* LCD en negro */
    frame(UI_OK);    CHECK(ctx.state == ST_LEVEL && ctx.select_song == LEVEL_DEMO && strcmp(ctx.song.name, "Demo") == 0);
    render_frame(&ctx);
    CHECK(stub_lcd_count(0xFFFF) == 0);                                        /* sin menú ni texto */
    frame(UI_OK);    CHECK(ctx.state == ST_PLAY && stub_play_mode == 1);
    stub_uart_out[0] = '\0';

    /* LCD: la 1.a flecha (UP, t = 3000 ms) se ve entre 2750 y 3250 ms. */
    while (stub_song_ms < 2700u) { frame(UI_NONE); render_frame(&ctx); }
    CHECK(stub_lcd_count(0xFFFF) == 0);
    while (stub_song_ms < 2760u) { frame(UI_NONE); render_frame(&ctx); }
    CHECK(stub_lcd_count(0xFFFF) > 0);

    /* Flecha sin nota cerca: se imprime sin puntaje. El autoplay no se imprime. */
    stub_push_joy_hit(IZQ, 2790u);
    stub_push_hit(ARRIBA, 3000u);                                              /* autoplay */
    frame(UI_NONE);  render_frame(&ctx);
    CHECK(strstr(stub_uart_out, "[JOY] LEFT  t=2790 ms\r\n") != NULL);
    CHECK(strstr(stub_uart_out, "[JOY] UP") == NULL && ctx.game_status.hits == 1);
    CHECK(stub_lcd_count(0xFFFF) > 0);                                         /* el acierto no la borra */
    while (stub_song_ms < 3240u) { frame(UI_NONE); render_frame(&ctx); }
    CHECK(stub_lcd_count(0xFFFF) > 0);
    while (stub_song_ms < 3300u) { frame(UI_NONE); render_frame(&ctx); }
    CHECK(stub_lcd_count(0xFFFF) == 0);                                        /* 500 ms y se borra */

    /* Golpes del joystick sobre las notas 2..5: ACIERTO +100; con 5 aciertos, barra llena. */
    for (i = 1; i < 5; i++) {
        while (stub_song_ms + 30u < ctx.song.chart[i].t_ms) frame(UI_NONE);
        stub_push_joy_hit((u8)ctx.song.chart[i].lane, ctx.song.chart[i].t_ms);
        frame(UI_NONE);
    }
    CHECK(strstr(stub_uart_out, "[JOY] DOWN  t=4000 ms  -> ACIERTO +100 (puntaje 200)\r\n") != NULL);
    CHECK(ctx.game_status.hits == STAR_STREAK && ctx.game_status.star_phase == STAR_READY);
    frame(UI_POWER_UP_ENABLE); CHECK(ctx.game_status.star_phase == STAR_ACTIVE);
    for (i = 5; i < 10; i++) {
        while (stub_song_ms + 30u < ctx.song.chart[i].t_ms) frame(UI_NONE);
        stub_push_joy_hit((u8)ctx.song.chart[i].lane, ctx.song.chart[i].t_ms);
        frame(UI_NONE);
    }
    CHECK(strstr(stub_uart_out, "[JOY] UP    t=12000 ms  -> ACIERTO +200 (puntaje 1500)\r\n") != NULL);

    /* Sin cuenta de 20 s: termina 2 s después de la última flecha (12000 + 300 + 2000). */
    CHECK(strstr(stub_uart_out, "quedan") == NULL);
    while (ctx.state == ST_PLAY) frame(UI_NONE);
    CHECK(ctx.result == RESULT_WIN && stub_song_ms > 14300u && stub_song_ms < 14400u);
    CHECK(strstr(stub_uart_out, "[GAMEOVER] puntaje final: 1500\r\n") != NULL);
    render_frame(&ctx);
    CHECK(stub_lcd_count(0xFFFF) == 0);
}
