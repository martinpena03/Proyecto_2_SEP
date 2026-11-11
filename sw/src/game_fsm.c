/*
 * game_fsm.c - Máquina de estados del juego (State_Machine, AC1).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (SepHero_BD.pdf página 3, docs/MANUAL_IMPLEMENTACION.md §9.2 y §9.3).
 *
 * Portable: habla con el hardware solo a través de interfaces.h. Se prueba en el PC
 * (tests/test_fsm.c) inyectando eventos.
 *
 * Estados: INIT -> MENU -> (RECORDS | CONFIG | LEVEL) ; LEVEL -> PLAY <-> PAUSE ;
 *          PLAY -> GAMEOVER -> MENU.
 * fsm_step() se llama una vez por frame (cada 30 ms) con a lo más un evento de interfaz.
 * Las flechas del juego no llegan como eventos: se leen de la cola de golpes (hit_pop).
 * El tema claro/oscuro cambia en paralelo a todos los estados (interrupción del OPT3001).
 */
#include "game_fsm.h"

/* -------------------------------------------------------------------------- */
/* Apoyo                                                                      */
/* -------------------------------------------------------------------------- */

/* Aplica una configuración entregada por la consola: escribe el IP propio a través de
 * debouncer_init() (función con punteros, AO3) y reparte los otros dos umbrales. */
int fsm_apply_cfg(GameCtx *ctx, const DebCfg *cfg)
{
    char b[96];
    char *p = b;
    u16 d = 0;
    u8  m = 0;
    int r = debouncer_init(&ctx->deb, cfg);

    ctx->cfg_status = r;
    if (r == 0) {
        ctx->cfg = *cfg;
        sensors_set_shake_threshold(cfg->umbral_shake);
        debouncer_readback(&ctx->deb, &d, &m);
        p = fmt_str(p, "IP: DEB_MS=");       p = fmt_u32(p, d);
        p = fmt_str(p, " BTN_MASK=0x");      p = fmt_hex(p, m, 1);
        p = fmt_str(p, " LED_CTRL=0x");      p = fmt_hex(p, debouncer_led_readback(&ctx->deb), 2);
        p = fmt_str(p, " (releidos del hardware)\r\n");
    } else {
        p = fmt_str(p, "IP: debouncer_init fallo, codigo -");
        p = fmt_u32(p, (u32)(-r));
        p = fmt_str(p, "\r\n");
    }
    uart_hal_puts(b);
    return r;
}

/* Carga la canción elegida y arma el preview (primeras notas con el tiempo desde 0). */
static void load_selected_song(GameCtx *ctx)
{
    u16 i, n;
    u32 t0;
    int r = sd_load_song(&ctx->sd, &ctx->song, ctx->pool, ctx->song_sel);

    ctx->song_ok = (u8)(r > 0);
    if (!ctx->song_ok) {
        buzzer_stop(&ctx->bz);
        return;
    }
    n  = (ctx->song.n < PREVIEW_N) ? ctx->song.n : (u16)PREVIEW_N;
    t0 = ctx->song.chart[0].t_ms;
    for (i = 0; i < n; i++) {
        ctx->preview[i] = ctx->song.chart[i];
        ctx->preview[i].t_ms = ctx->song.chart[i].t_ms - t0 + 300u;   /* 300 ms de respiro */
    }
    buzzer_play_jingle(&ctx->bz, ctx->preview, n, 1);
}

/* ¿El puntaje entra al top 5? */
static int score_qualifies(const GameCtx *ctx, u32 score)
{
    if (score == 0u) return 0;
    if (ctx->n_rec < MAX_RECORDS) return 1;
    return score > ctx->rec[MAX_RECORDS - 1u].score;
}

/* Representación del estado por consola (avance): nombre y teclas que lo mueven.
 * Teclas de la consola de diagnóstico: 1 = OK (S1), 2 = atrás (S2), u / j = arriba / abajo. */
static void announce_state(FsmState s)
{
    static const char *const NAME[] = {
        "INIT", "MENU", "LEVEL (seleccion de cancion)", "PLAY (jugando)", "PAUSE",
        "GAMEOVER", "RECORDS", "CONFIG (parametros por UART)" };
    static const char *const HINT[] = {
        "inicio; pasa solo al MENU en 1,5 s",
        "u/j (o JOY_SEL) mueve el cursor (Jugar, Records, Configurar), 1 (o S1) entra",
        "u/j cambia la cancion (suena el preview), 1 juega, 2 (o S2) vuelve al MENU",
        "p = autoplay (sin joystick en el avance), sacudir la placa = star power, 2 = PAUSE",
        "u/j elige (Continuar, Reiniciar, Salir), 1 confirma, 2 continua",
        "1 o 2 vuelve al MENU",
        "1 o 2 vuelve al MENU",
        "escribir los valores y Enter; S2 en la placa cancela" };
    char b[200];
    char *p = b;
    u8 i = ((unsigned)s < 8u) ? (u8)s : 0u;
    p = fmt_str(p, "\r\n==================== ESTADO: ");
    p = fmt_str(p, NAME[i]);
    p = fmt_str(p, " ====================\r\n   ");
    p = fmt_str(p, HINT[i]);
    p = fmt_str(p, "\r\n");
    uart_hal_puts(b);
}

/* Cambia de estado: guarda el anterior, sella el tiempo, pide redibujo completo y
 * ejecuta la acción de entrada del estado nuevo. */
static void enter_state(GameCtx *ctx, FsmState ns)
{
    if (ctx->st == ST_PLAY && ns != ST_PLAY) input_set_play_mode(0);   /* salir de JUEGO */
    ctx->prev_st = ctx->st;
    ctx->st      = ns;
    ctx->t_state = clock_sys_ms();
    ctx->redraw_seq++;
    announce_state(ns);

    switch (ns) {
    case ST_INIT:
        break;
    case ST_MENU:
        ctx->cursor = 0;
        buzzer_play_jingle(&ctx->bz, JINGLE_MENU, JINGLE_MENU_N, 1);
        break;
    case ST_RECORDS:
        buzzer_stop(&ctx->bz);
        break;
    case ST_CONFIG:
        buzzer_stop(&ctx->bz);
        uart_cfg_begin(&ctx->ucfg, &ctx->cfg);
        break;
    case ST_LEVEL:
        load_selected_song(ctx);
        break;
    case ST_PLAY:
        input_flush();
        input_set_play_mode(1);                 /* el joystick pasa a generar golpes */
        break;
    case ST_PAUSE:
        clock_song_run(0);                      /* congela song_ms */
        buzzer_pause(&ctx->bz, 1);
        ctx->cursor = 0;
        break;
    case ST_GAMEOVER:
        clock_song_run(0);
        buzzer_stop(&ctx->bz);
        if (ctx->result == RESULT_WIN) buzzer_play_jingle(&ctx->bz, JINGLE_WIN, JINGLE_WIN_N, 0);
        else                           buzzer_play_jingle(&ctx->bz, JINGLE_LOSE, JINGLE_LOSE_N, 0);
        ctx->name[0] = 'A'; ctx->name[1] = 'A'; ctx->name[2] = 'A'; ctx->name[3] = '\0';
        ctx->name_pos   = 0;
        ctx->rec_pos    = -1;
        ctx->encore_cnt = 0;
        ctx->name_entry = (u8)score_qualifies(ctx, ctx->gs.score);
        break;
    default:
        break;
    }
}

/* Partida nueva (desde la selección o "Reiniciar"). */
static void start_game(GameCtx *ctx)
{
    game_reset(&ctx->gs);
    ctx->count_shown = 0;
    clock_song_set(0);
    clock_song_run(1);
    buzzer_play(&ctx->bz, &ctx->song);
    enter_state(ctx, ST_PLAY);
}

/* Continuar tras la pausa. */
static void resume_game(GameCtx *ctx)
{
    buzzer_pause(&ctx->bz, 0);
    clock_song_run(1);
    enter_state(ctx, ST_PLAY);
}

/* -------------------------------------------------------------------------- */
/* Interfaz pública                                                           */
/* -------------------------------------------------------------------------- */

/* Arranque: monta la SD (o activa el respaldo), carga los récords y muestra el logo. */
void fsm_start(GameCtx *ctx)
{
    (void)sd_init(&ctx->sd);
    ctx->n_rec    = (u8)sd_load_records(&ctx->sd, ctx->rec, (u8)MAX_RECORDS);
    ctx->song_sel = 0;
    ctx->rec_pos  = -1;
    ctx->st       = ST_INIT;
    enter_state(ctx, ST_INIT);
}

void fsm_step(GameCtx *ctx, UiEvent ev)
{
    u32 now_sys = clock_sys_ms();

    /* En paralelo a todos los estados: tema (OPT3001) y nivel del público (micrófono).
     * El tema habilita el modo claro en el IP propio (LEDs LD0..LD3 con el patrón de la
     * consola); más adelante el mismo bit cambia la paleta del LCD. */
    {
        u8 th = (u8)theme_current();
        if (th != ctx->gs.theme || ctx->deb.light != (u8)(th == THEME_LIGHT)) {
            debouncer_set_theme(&ctx->deb, th == THEME_LIGHT);
        }
        ctx->gs.theme = th;
    }
    ctx->gs.crowd = sensors_crowd_level();

    switch (ctx->st) {
    /* ------------------------------------------------------------------ INIT */
    case ST_INIT:
        if (now_sys - ctx->t_state >= 1500u) enter_state(ctx, ST_MENU);
        break;

    /* ------------------------------------------------------------------ MENU */
    case ST_MENU:
        if (ev == UI_UP)        ctx->cursor = (u8)((ctx->cursor + MENU_ITEMS - 1u) % MENU_ITEMS);
        else if (ev == UI_DOWN) ctx->cursor = (u8)((ctx->cursor + 1u) % MENU_ITEMS);
        else if (ev == UI_OK) {
            if (ctx->cursor == 0u)      enter_state(ctx, ST_LEVEL);
            else if (ctx->cursor == 1u) enter_state(ctx, ST_RECORDS);
            else                        enter_state(ctx, ST_CONFIG);
        }
        break;

    /* --------------------------------------------------------------- RECORDS */
    case ST_RECORDS:
        if (ev == UI_BACK || ev == UI_OK) enter_state(ctx, ST_MENU);
        break;

    /* ---------------------------------------------------------------- CONFIG */
    case ST_CONFIG: {
        DebCfg ncfg;
        if (uart_cfg_poll(&ctx->ucfg, &ncfg) == 1) {
            (void)fsm_apply_cfg(ctx, &ncfg);        /* debouncer_init(&deb, &cfg) */
            enter_state(ctx, ST_MENU);
        } else if (ev == UI_BACK) {
            uart_hal_puts("\r\n(configuracion cancelada)\r\n");
            enter_state(ctx, ST_MENU);
        }
        break;
    }

    /* ----------------------------------------------------------------- LEVEL */
    case ST_LEVEL:
        if (ev == UI_UP || ev == UI_DOWN) {
            if (ev == UI_DOWN) ctx->song_sel = (u8)((ctx->song_sel + 1u) % N_SONGS);
            else               ctx->song_sel = (u8)((ctx->song_sel + N_SONGS - 1u) % N_SONGS);
            load_selected_song(ctx);
            ctx->redraw_seq++;
        } else if (ev == UI_OK) {
            if (ctx->song_ok) start_game(ctx);
        } else if (ev == UI_BACK) {
            enter_state(ctx, ST_MENU);
        }
        break;

    /* ------------------------------------------------------------------ PLAY */
    case ST_PLAY: {
        Hit h;
        u32 now;
        int res;

        while (hit_pop(&h)) (void)game_judge_hit(&ctx->gs, &ctx->song, &h);   /* 1.o los golpes */
        now = clock_song_ms();
        game_update(&ctx->gs, &ctx->song, now);                              /* 2.o el tiempo  */

        /* Cuenta 3-2-1 durante el lead-in: un beep por segundo. */
        if (now < LEADIN_MS) {
            u8 k = (u8)(now / 1000u + 1u);
            if (k != ctx->count_shown) {
                ctx->count_shown = k;
                buzzer_play_sfx(&ctx->bz, SFX_COUNT, SFX_COUNT_N);
            }
        }

        if (ev == UI_SHAKE) {                                               /* acelerómetro */
            if (game_try_star_power(&ctx->gs)) buzzer_play_sfx(&ctx->bz, SFX_STAR, SFX_STAR_N);
        } else if (ev == UI_BACK) {
            enter_state(ctx, ST_PAUSE);
            break;
        }

        res = game_is_over(&ctx->gs, &ctx->song, now);
        if (res != RESULT_NONE) {
            ctx->result = (u8)res;
            enter_state(ctx, ST_GAMEOVER);
        }
        break;
    }

    /* ----------------------------------------------------------------- PAUSE */
    case ST_PAUSE:
        if (ev == UI_UP)        ctx->cursor = (u8)((ctx->cursor + PAUSE_ITEMS - 1u) % PAUSE_ITEMS);
        else if (ev == UI_DOWN) ctx->cursor = (u8)((ctx->cursor + 1u) % PAUSE_ITEMS);
        else if (ev == UI_BACK) resume_game(ctx);
        else if (ev == UI_OK) {
            if (ctx->cursor == 0u)      resume_game(ctx);              /* Continuar */
            else if (ctx->cursor == 1u) start_game(ctx);               /* Reiniciar */
            else {                                                     /* Salir     */
                buzzer_stop(&ctx->bz);
                enter_state(ctx, ST_MENU);
            }
        }
        break;

    /* -------------------------------------------------------------- GAMEOVER */
    case ST_GAMEOVER:
        /* Bonus encore: un grito sostenido durante los primeros 5 s. */
        if (!ctx->gs.encore_done && ctx->rec_pos < 0 && (now_sys - ctx->t_state) <= ENCORE_WIN_MS) {
            if (ctx->gs.crowd >= ENCORE_LEVEL) {
                if (++ctx->encore_cnt >= ENCORE_FRAMES) {
                    ctx->gs.score += ctx->gs.score * ENCORE_BONUS_PCT / 100u;
                    ctx->gs.encore_done = 1;
                    buzzer_play_sfx(&ctx->bz, SFX_ENCORE, SFX_ENCORE_N);
                    ctx->name_entry = (u8)score_qualifies(ctx, ctx->gs.score);
                    ctx->redraw_seq++;
                }
            } else {
                ctx->encore_cnt = 0;
            }
        }

        if (ctx->name_entry) {                       /* ingreso de iniciales al top 5 */
            char *c = &ctx->name[ctx->name_pos];
            if (ev == UI_UP)        *c = (char)((*c == 'Z') ? 'A' : *c + 1);
            else if (ev == UI_DOWN) *c = (char)((*c == 'A') ? 'Z' : *c - 1);
            else if (ev == UI_OK) {
                if (++ctx->name_pos >= 3u) {
                    ctx->rec_pos = (s8)records_insert(ctx->rec, &ctx->n_rec, ctx->name, ctx->gs.score);
                    (void)sd_save_records(&ctx->sd, ctx->rec, ctx->n_rec);
                    ctx->name_entry = 0;
                    ctx->redraw_seq++;
                }
            }
        } else if (ev == UI_OK || ev == UI_BACK) {
            enter_state(ctx, ST_MENU);
        }
        break;

    default:
        enter_state(ctx, ST_MENU);
        break;
    }
}
