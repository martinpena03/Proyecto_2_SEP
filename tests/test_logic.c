/*
 * test_logic.c - Tests de game_engine, joy_logic, sd_formats, audio_seq, debcfg y
 * uart_config (docs/MANUAL_IMPLEMENTACION.md §9.1, §10.3 y §12.1).
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio.
 */
#include <string.h>
#include "test.h"

/* Chart de prueba: notas cada 1000 ms alternando carriles. */
static Note pool[MAX_NOTES];
static Song song;
static void make_song(u16 n)
{
    u16 i;
    for (i = 0; i < n; i++) {
        pool[i].t_ms = 3000u + 1000u * i;
        pool[i].lane = (Lane)(i % 4u);
        pool[i].freq = 1000;
        pool[i].dur  = 400;
    }
    strcpy(song.name, "TEST");
    song.bpm = 60; song.n = n; song.chart = pool;
}
static Hit mk(u8 lane, u32 t) { Hit h; h.lane = lane; h.song_ms = t; return h; }

/* ------------------------------------------------------------ game_engine */
void test_engine(void)
{
    GameState gs;
    Hit h;
    u16 i;
    memset(&gs, 0, sizeof gs);
    make_song(30);

    gs.theme = THEME_LIGHT;
    game_reset(&gs);
    CHECK(gs.rock == ROCK_START && gs.score == 0 && gs.theme == THEME_LIGHT);

    /* Acierto justo en los bordes de la ventana (+-120 ms) y fallo fuera de ella. */
    h = mk(0, 3000 - HIT_WIN_MS);          CHECK(game_judge_hit(&gs, &song, &h) == 1);
    h = mk(1, 4000 + HIT_WIN_MS);          CHECK(game_judge_hit(&gs, &song, &h) == 1);
    h = mk(2, 5000 + HIT_WIN_MS + 1);      CHECK(game_judge_hit(&gs, &song, &h) == 0);
    CHECK(gs.score == 2 * PTS_NOTA && gs.streak == 2 && gs.rock == ROCK_START + 2);

    /* Doble golpe sobre la misma nota: el segundo no puntúa. */
    h = mk(0, 3000);                       CHECK(game_judge_hit(&gs, &song, &h) == 0);
    CHECK(gs.score == 2 * PTS_NOTA);

    /* Flecha equivocada con una nota en la ventana: rock -2, racha 0, la nota sigue. */
    h = mk(3, 5000);                       CHECK(game_judge_hit(&gs, &song, &h) == 0);
    CHECK(gs.rock == ROCK_START && gs.streak == 0 && gs.rcv_fx_kind[3] == 2);
    h = mk(2, 5010);                       CHECK(game_judge_hit(&gs, &song, &h) == 1);

    /* Flecha sin notas cerca: sin efecto. */
    h = mk(1, 5500);                       CHECK(game_judge_hit(&gs, &song, &h) == 0);
    CHECK(gs.rock == ROCK_START + 1 && gs.streak == 1);

    /* Nota perdida: rock -2, racha 0 y next_note avanza. */
    game_update(&gs, &song, 6000 + HIT_WIN_MS + 1);
    CHECK(gs.misses == 1 && gs.streak == 0 && gs.rock == ROCK_START - 1 && gs.next_note == 4);

    /* 10 aciertos seguidos => READY; el rock se satura en ROCK_MAX. */
    game_reset(&gs);
    for (i = 0; i < STAR_STREAK; i++) {
        h = mk((u8)(i % 4u), 3000u + 1000u * i);
        CHECK(game_judge_hit(&gs, &song, &h) == 1);
    }
    game_update(&gs, &song, 12050);
    CHECK(gs.star_phase == STAR_READY && gs.star_bar == 100 && gs.rock == ROCK_MAX);
    CHECK(gs.max_streak == STAR_STREAK);

    /* Sacudida: en READY activa; puntaje doble mientras está ACTIVE. */
    CHECK(game_try_star_power(&gs) == 1 && gs.star_phase == STAR_ACTIVE && gs.star_ms == STAR_MS);
    CHECK(game_try_star_power(&gs) == 0);
    {
        u32 before = gs.score;
        h = mk(2, 13000);                  CHECK(game_judge_hit(&gs, &song, &h) == 1);
        CHECK(gs.score == before + 2 * PTS_NOTA);
    }
    gs.star_ms = STAR_MS / 2;  game_update(&gs, &song, 13050);  CHECK(gs.star_bar == 50);
    gs.star_ms = 0;            game_update(&gs, &song, 13060);
    CHECK(gs.star_phase == STAR_CHARGING && gs.charge == 0 && gs.star_bar == 0);

    /* Sacudida en CHARGING no hace nada. */
    CHECK(game_try_star_power(&gs) == 0);

    /* Fin por rock = 0 y fin de canción. */
    game_reset(&gs);
    CHECK(game_is_over(&gs, &song, 5000) == RESULT_NONE);
    game_update(&gs, &song, 3000 + 1000 * 6);              /* 6 notas perdidas: 10 - 12 */
    CHECK(gs.rock == 0 && game_is_over(&gs, &song, 9000) == RESULT_LOSE);
    game_reset(&gs);
    CHECK(song_duration_ms(&song) == 32000 + 400 + SONG_TAIL_MS);
    CHECK(game_is_over(&gs, &song, song_duration_ms(&song)) == RESULT_NONE);
    CHECK(game_is_over(&gs, &song, song_duration_ms(&song) + 1) == RESULT_WIN);

    /* Los destellos duran 4 frames. */
    game_reset(&gs);
    h = mk(0, 3000); (void)game_judge_hit(&gs, &song, &h);
    CHECK(gs.rcv_fx[0] == 4 && gs.rcv_fx_kind[0] == 1);
    for (i = 0; i < 4; i++) game_update(&gs, &song, 3000);
    CHECK(gs.rcv_fx[0] == 0);
}

/* -------------------------------------------------------------- joy_logic */
void test_joy(void)
{
    JoyDet d;
    joy_det_init(&d, 512, 512, 250, 0);
    CHECK(joy_detect(&d, 512, 512) == -1);                 /* centro */
    CHECK(joy_detect(&d, 700, 520) == -1);                 /* bajo el umbral */
    CHECK(joy_detect(&d, 900, 520) == DER);
    CHECK(joy_detect(&d, 900, 520) == -1);                 /* mantener no repite */
    CHECK(joy_detect(&d, 700, 512) == -1);                 /* dentro de la histéresis: sigue */
    CHECK(joy_detect(&d, 900, 512) == -1);
    CHECK(joy_detect(&d, 600, 512) == -1);                 /* bajo umbral - hist: rearma */
    CHECK(joy_detect(&d, 100, 512) == IZQ);
    (void)joy_detect(&d, 512, 512);
    CHECK(joy_detect(&d, 512, 900) == ARRIBA);
    (void)joy_detect(&d, 512, 512);
    CHECK(joy_detect(&d, 512, 100) == ABAJO);
    (void)joy_detect(&d, 512, 512);
    CHECK(joy_detect(&d, 900, 850) == DER);                /* diagonal: eje dominante X */
    (void)joy_detect(&d, 512, 512);
    CHECK(joy_detect(&d, 800, 100) == ABAJO);              /* diagonal: eje dominante Y */
    /* Centro calibrado distinto de 512 y eje Y invertido. */
    joy_det_init(&d, 500, 530, 250, 1);
    CHECK(joy_detect(&d, 500, 900) == ABAJO);
    (void)joy_detect(&d, 500, 530);
    CHECK(joy_detect(&d, 500, 150) == ARRIBA);
}

/* ------------------------------------------------------------- sd_formats */
void test_formats(void)
{
    static Note p2[MAX_NOTES];
    Song s;
    Record r[MAX_RECORDS];
    char buf[256];
    u8 n = 0;
    u32 len;
    const char *ok = "# c\r\nNAME=Prueba uno \r\nBPM=90\r\n\r\n3000 0 1319 450\r\n3500 3 0 200 # sil\n4000 1 1397 450";
    const char *desorden = "3000 0 1000 100\n2000 1 1000 100\n";
    const char *carril = "3000 4 1000 100\n";
    const char *basura = "3000 0 1000\n";

    CHECK(song_parse(ok, (u32)strlen(ok), &s, p2, MAX_NOTES) == 3);
    CHECK(strcmp(s.name, "Prueba uno") == 0 && s.bpm == 90 && s.n == 3 && s.chart == p2);
    CHECK(p2[1].t_ms == 3500 && p2[1].lane == DER && p2[1].freq == 0 && p2[1].dur == 200);
    CHECK(song_parse(desorden, (u32)strlen(desorden), &s, p2, MAX_NOTES) == -2);
    CHECK(song_parse(carril, (u32)strlen(carril), &s, p2, MAX_NOTES) == -4);
    CHECK(song_parse(basura, (u32)strlen(basura), &s, p2, MAX_NOTES) == -1);
    CHECK(song_parse("", 0, &s, p2, MAX_NOTES) == -5);
    CHECK(song_parse(ok, (u32)strlen(ok), &s, p2, 2) == -3);                 /* excede el pool */
    /* Los charts embebidos son válidos y respetan el lead-in. */
    CHECK(song_parse(g_song1_shc, g_song1_shc_len, &s, p2, MAX_NOTES) == 62);
    CHECK(p2[0].t_ms >= LEADIN_MS && strcmp(s.name, "Oda Alegria") == 0);
    CHECK(song_parse(g_song2_shc, g_song2_shc_len, &s, p2, MAX_NOTES) == 74);

    /* Récords: líneas basura ignoradas, orden descendente, máximo 5. */
    {
        const char *t = "# hdr\nABC 100\nxx\nMPZ 18400\nAB 5\nZZZ 700\nQ1Q 50\nAAA 60\nBBB 70\nCCC 999999\n";
        CHECK(records_parse(t, (u32)strlen(t), r, MAX_RECORDS) == 5);
        CHECK(r[0].score == 999999 && strcmp(r[0].name, "CCC") == 0);
        CHECK(r[1].score == 18400 && r[4].score == 70);
    }
    n = 5;
    CHECK(records_insert(r, &n, "NEW", 60) == -1 && n == 5);                 /* no entra */
    CHECK(records_insert(r, &n, "NEW", 800) == 2 && n == 5);
    CHECK(strcmp(r[2].name, "NEW") == 0 && r[3].score == 700 && r[4].score == 100);
    len = records_format(r, n, buf, sizeof buf - 1);
    buf[len] = '\0';
    {
        Record r2[MAX_RECORDS];
        CHECK(records_parse(buf, len, r2, MAX_RECORDS) == 5);                /* ida y vuelta */
        CHECK(memcmp(r, r2, sizeof r) == 0);
    }
    n = 0;
    CHECK(records_insert(r, &n, "ONE", 10) == 0 && n == 1);
}

/* -------------------------------------------------------------- audio_seq */
void test_audio_seq(void)
{
    Buzzer_t bz;
    u32 t, calls;
    make_song(3);                                           /* notas en 3000, 4000, 5000 */
    buzzer_init(&bz, 50);
    buzzer_play(&bz, &song);
    for (t = 0; t < 3000; t++) buzzer_tick(&bz, t);
    CHECK(stub_tone_f == 0);
    buzzer_tick(&bz, 3000);       CHECK(stub_tone_f == 1000);
    calls = stub_tone_calls;
    for (t = 3001; t < 3400; t++) buzzer_tick(&bz, t);
    CHECK(stub_tone_calls == calls);                        /* no reescribe el TTC */
    buzzer_tick(&bz, 3400);       CHECK(stub_tone_f == 0);  /* fin de la nota */
    /* Pausa: silencio aunque el reloj esté dentro de una nota. */
    buzzer_tick(&bz, 4100);       CHECK(stub_tone_f == 1000);
    buzzer_pause(&bz, 1);  buzzer_tick(&bz, 4100);  CHECK(stub_tone_f == 0);
    buzzer_pause(&bz, 0);  buzzer_tick(&bz, 4100);  CHECK(stub_tone_f == 1000);
    /* Efecto superpuesto: tapa la pista y luego esta vuelve. */
    buzzer_play_sfx(&bz, SFX_COUNT, SFX_COUNT_N);
    buzzer_tick(&bz, 4101);       CHECK(stub_tone_f == SFX_COUNT[0].freq);
    for (t = 0; t < 130; t++) buzzer_tick(&bz, 4102);
    CHECK(stub_tone_f == 1000 && bz.sfx_on == 0);
    buzzer_stop(&bz);             CHECK(stub_tone_f == 0 && bz.playing == 0);
    /* Jingle sin loop termina solo; con loop vuelve a empezar. */
    buzzer_play_jingle(&bz, JINGLE_WIN, JINGLE_WIN_N, 0);
    for (t = 0; t < 1300; t++) buzzer_tick(&bz, 0);
    CHECK(bz.playing == 0 && stub_tone_f == 0);
    buzzer_play_jingle(&bz, JINGLE_MENU, JINGLE_MENU_N, 1);
    for (t = 0; t < 4005; t++) buzzer_tick(&bz, 0);
    CHECK(bz.playing == 1 && stub_tone_f == JINGLE_MENU[0].freq);
}

/* ----------------------------------------------------------------- debcfg */
void test_debcfg(void)
{
    DebCfg c;
    debcfg_default(&c);
    CHECK(debcfg_validate(&c) == 0 && c.deb_ms == 20 && c.btn_mask == 7);
    CHECK(debcfg_validate(NULL) == -1);
    c.deb_ms = 0;      CHECK(debcfg_validate(&c) == -2);   c.deb_ms = 255;
    c.btn_mask = 0;    CHECK(debcfg_validate(&c) == -3);
    c.btn_mask = 2;    CHECK(debcfg_validate(&c) == -4);   c.btn_mask = 4;
    CHECK(debcfg_validate(&c) == 0);
    c.joy_umbral = 99; CHECK(debcfg_validate(&c) == -5);   c.joy_umbral = 450;
    c.umbral_shake = 2001; CHECK(debcfg_validate(&c) == -6);
}

/* ------------------------------------------------------------ uart_config */
void test_uart_cfg(void)
{
    UartCfg u;
    DebCfg cur, out;
    debcfg_default(&cur);
    memset(&out, 0, sizeof out);

    /* Caso normal con CR LF, retroceso y una línea vacía (conserva el valor). */
    uart_cfg_begin(&u, &cur);
    CHECK(uart_cfg_poll(&u, &out) == 0);                    /* sin datos: no bloquea */
    stub_uart_feed("15\r\n");
    CHECK(uart_cfg_poll(&u, &out) == 0 && u.step == 1);
    stub_uart_feed("0x5\r");
    CHECK(uart_cfg_poll(&u, &out) == 0 && u.step == 2);
    stub_uart_feed("\n22");                                 /* '\n' suelto del par CR LF */
    CHECK(uart_cfg_poll(&u, &out) == 0 && u.step == 2 && u.len == 2);
    stub_uart_feed("9\b0\r\n");                             /* 229 -> retroceso -> 220 */
    CHECK(uart_cfg_poll(&u, &out) == 0 && u.step == 3);
    stub_uart_feed("\r");                                   /* vacío: conserva 800 */
    CHECK(uart_cfg_poll(&u, &out) == 1);
    CHECK(out.deb_ms == 15 && out.btn_mask == 5 && out.joy_umbral == 220 && out.umbral_shake == 800);
    CHECK(strstr(stub_uart_out, "OK -> DEB_MS=15 BTN_MASK=0x5 JOY_UMBRAL=220 UMBRAL_SHAKE=800 mg") != NULL);

    /* Valores inválidos: repite el paso. */
    stub_uart_out[0] = '\0';
    uart_cfg_begin(&u, &cur);
    stub_uart_feed("0\n");      CHECK(uart_cfg_poll(&u, &out) == 0 && u.step == 0);
    stub_uart_feed("abc\n");    CHECK(uart_cfg_poll(&u, &out) == 0 && u.step == 0);
    stub_uart_feed("300\n");    CHECK(uart_cfg_poll(&u, &out) == 0 && u.step == 0);
    CHECK(strstr(stub_uart_out, "Valor fuera de rango") != NULL);
    stub_uart_feed("30\n2\n");  CHECK(uart_cfg_poll(&u, &out) == 0 && u.step == 1);   /* máscara sin OK */
    stub_uart_feed("7\n99\n");  CHECK(uart_cfg_poll(&u, &out) == 0 && u.step == 2);
    stub_uart_feed("300\n5000\n1500\n");
    CHECK(uart_cfg_poll(&u, &out) == 1 && out.deb_ms == 30 && out.joy_umbral == 300 && out.umbral_shake == 1500);
}
