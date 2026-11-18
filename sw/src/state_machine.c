/*
 * state_machine.c - Máquina de estados del juego (State_Machine, AC1).
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
#include "state_machine.h"

/* Game_on_off (State_Machine -> Game Manager): 1 = la partida corre (song_ms avanza),
 * 0 = congelada (PAUSA, GAME OVER). */
static void game_on_off(int on)
{
    clock_song_run(on);
}

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

/* Carga la canción elegida. En el avance los niveles no suenan (ni preview ni canción):
 * solo existen en la máquina de estados, así que se informa por consola cuál quedó. */
static void load_selected_song(GameCtx *ctx)
{
    char b[96];
    char *p = b;
    int r = sd_load_song(&ctx->sd, &ctx->song, ctx->pool, ctx->select_song);

    ctx->song_ok = (u8)(r > 0);
    buzzer_stop(&ctx->bz);                       /* la melodía del menú no sigue aquí */
    p = fmt_str(p, "   [LEVEL]  ");
    p = fmt_u32(p, (u32)ctx->select_song + 1u);
    p = fmt_str(p, "/");
    p = fmt_u32(p, N_SONGS);
    if (ctx->song_ok) {
        p = fmt_str(p, ": ");
        p = fmt_str(p, ctx->song.name);
        p = fmt_str(p, " (");
        p = fmt_u32(p, ctx->song.n);
        p = fmt_str(p, " notas). Musica de juego pendiente\r\n");
    } else {
        p = fmt_str(p, ": error de carga\r\n");
    }
    uart_hal_puts(b);
}

/* Cuenta regresiva de PLAY por consola: una línea por segundo de canción. */
static void announce_countdown(u8 left)
{
    char b[48];
    char *p = b;
    p = fmt_str(p, "   [Jugando] quedan ");
    p = fmt_u32(p, left);
    p = fmt_str(p, " s\r\n");
    uart_hal_puts(b);
}

/* Imprime cada flecha del joystick detectada durante el juego y, si coincide con la nota
 * esperada, los puntos que sumó (PTS_NOTA, x2 con star power).
 * Ej.: "[JOY] UP    t=3000 ms  -> ACIERTO +100 (puntaje 100)". */
static void announce_hit(const Hit_signal *hit_signal, u32 pts, u32 score)
{
    char b[80];
    char *p = b;
    p = fmt_str(p, "[JOY] ");
    p = fmt_str(p, lane_name(hit_signal->lane));
    while (p < b + 12) *p++ = ' ';                  /* columna fija tras "RIGHT" */
    p = fmt_str(p, "t=");
    p = fmt_u32(p, hit_signal->t_ms);
    p = fmt_str(p, " ms");
    if (pts > 0u) {
        p = fmt_str(p, "  -> ACIERTO +");
        p = fmt_u32(p, pts);
        p = fmt_str(p, " (puntaje ");
        p = fmt_u32(p, score);
        p = fmt_str(p, ")");
    }
    p = fmt_str(p, "\r\n");
    uart_hal_puts(b);
}

/* Puntaje al terminar la partida (Game_over_signal). */
static void announce_final_score(u32 score)
{
    char b[48];
    char *p = b;
    p = fmt_str(p, "   [GAMEOVER] puntaje final: ");
    p = fmt_u32(p, score);
    p = fmt_str(p, "\r\n");
    uart_hal_puts(b);
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
        "INIT", "MENU", "LEVEL ", " [jugando]", "[Pausa]",
        "GAMEOVER", "RECORDS", "CONFIGURACION" };
    static const char *const HINT[] = {
        "inicio",
        "1 para continuar, JOY_SEL para musica ON/OFF",
        "u/j navegacion, 1 para jugar, 2 para volver al menu",
        "20 s de partida, para PAUSE",
        "u/j navegacion: Continuar, Reiniciar, Salir, 1 aceptar, 2 continua",
        "1 o 2 vuelve al MENU",
        "1 o 2 vuelve al MENU",
        "escribe los valores y presiona enter, 2 para salir" };
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
    if (ctx->state == ST_PLAY && ns != ST_PLAY) input_set_play_mode(0);   /* salir de JUEGO */
    ctx->prev_state = ctx->state;
    ctx->state      = ns;
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
        game_on_off(0);                      /* congela song_ms */
        buzzer_pause(&ctx->bz, 1);
        ctx->cursor = 0;
        break;
    case ST_GAMEOVER:
        game_on_off(0);
        announce_final_score(ctx->game_status.score);
        buzzer_stop(&ctx->bz);
        if (ctx->result == RESULT_WIN) buzzer_play_jingle(&ctx->bz, JINGLE_WIN, JINGLE_WIN_N, 0);
        else                           buzzer_play_jingle(&ctx->bz, JINGLE_LOSE, JINGLE_LOSE_N, 0);
        ctx->name[0] = 'A'; ctx->name[1] = 'A'; ctx->name[2] = 'A'; ctx->name[3] = '\0';
        ctx->name_pos   = 0;
        ctx->rec_pos    = -1;
        ctx->encore_cnt = 0;
        ctx->name_entry = (u8)score_qualifies(ctx, ctx->game_status.score);
        break;
    default:
        break;
    }
}

static void start_game(GameCtx *ctx)
{
    game_reset(&ctx->game_status);
    ctx->count_shown = 0;
    clock_song_set(0);
    game_on_off(1);
    buzzer_stop(&ctx->bz);                      /* avance: el nivel no tiene música */
    enter_state(ctx, ST_PLAY);
    uart_hal_puts("Juego iniciado\r\n");
}

/* Continuar tras la pausa. */
static void resume_game(GameCtx *ctx)
{
    buzzer_pause(&ctx->bz, 0);
    game_on_off(1);
    enter_state(ctx, ST_PLAY);
}



/* Arranque: monta la SD (o activa el respaldo), carga los récords y muestra el logo. */
void fsm_start(GameCtx *ctx)
{
    (void)sd_init(&ctx->sd);
    ctx->n_rec    = (u8)sd_load_records(&ctx->sd, ctx->rec, (u8)MAX_RECORDS);
    ctx->select_song = 0;
    ctx->rec_pos  = -1;
    ctx->state       = ST_INIT;
    enter_state(ctx, ST_INIT);
}

void fsm_step(GameCtx *ctx, UiEvent ev)
{
    u32 now_sys = clock_sys_ms();

    /* En paralelo a todos los estados: tema (OPT3001) y nivel del público (micrófono).
     * El tema habilita el modo claro en el IP propio (LEDs LD0..LD3 con el patrón de la
     * consola); más adelante el mismo bit cambia la paleta del LCD. */
    {
        u8 th = (u8)light_mode_current();
        if (th != ctx->game_status.light_mode || ctx->deb.light != (u8)(th == THEME_LIGHT)) {
            debouncer_set_theme(&ctx->deb, th == THEME_LIGHT);
        }
        ctx->game_status.light_mode = th;
    }
    ctx->game_status.crowd_noise = sensors_crowd_noise();

    switch (ctx->state) {
    /*  INIT */
    case ST_INIT:
        if (now_sys - ctx->t_state >= 1500u) enter_state(ctx, ST_MENU);
        break;

    /*  MENU */
    case ST_MENU:
        if (ev == UI_UP)        ctx->cursor = (u8)((ctx->cursor + MENU_ITEMS - 1u) % MENU_ITEMS);
        else if (ev == UI_DOWN) ctx->cursor = (u8)((ctx->cursor + 1u) % MENU_ITEMS);
        else if (ev == UI_OK) {
            if (ctx->cursor == 0u)      enter_state(ctx, ST_LEVEL);
            else if (ctx->cursor == 1u) enter_state(ctx, ST_RECORDS);
            else                        enter_state(ctx, ST_CONFIG);
        }
        break;

    /*  RECORDS */
    case ST_RECORDS:
        if (ev == UI_BACK || ev == UI_OK) enter_state(ctx, ST_MENU);
        break;

    /*   CONFIG */
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

    /*  LEVEL */
    case ST_LEVEL:
        if (ev == UI_UP || ev == UI_DOWN) {
            if (ev == UI_DOWN) ctx->select_song = (u8)((ctx->select_song + 1u) % N_SONGS);
            else               ctx->select_song = (u8)((ctx->select_song + N_SONGS - 1u) % N_SONGS);
            load_selected_song(ctx);
            ctx->redraw_seq++;
        } else if (ev == UI_OK) {
            if (ctx->song_ok) start_game(ctx);
        } else if (ev == UI_BACK) {
            enter_state(ctx, ST_MENU);
        }
        break;

    /*   PLAY */
    case ST_PLAY: {
        Hit_signal hit_signal;                     /* Hit_signal (lane, t_ms) del Joystick_aconditioner */
        u32 now;
        int game_over_signal;               /* Game_over_signal: GameResult                      */
        int demo = (ctx->select_song == LEVEL_DEMO);

        while (hit_pop(&hit_signal)) {                                       /* 1.o los golpes */
            u32 score0 = ctx->game_status.score;
            int acierto = game_judge_hit(&ctx->game_status, &ctx->song, &hit_signal);
            if (hit_signal.from_joy) {
                announce_hit(&hit_signal, acierto ? ctx->game_status.score - score0 : 0u, ctx->game_status.score);
            }
        }
        now = clock_song_ms();
        game_update(&ctx->game_status, &ctx->song, now);                              /* 2.o el tiempo  */

        /* por ahora la partida dura AVANCE_PLAY_MS en cada segundo se imprime cuanto falta y no hay sonido en el nivel. */
        /* El Demo no tiene cuenta de 20 s: termina 2 s después de su última flecha. */
        if (!demo && now < AVANCE_PLAY_MS) {
            u8 left = (u8)((AVANCE_PLAY_MS - now + 999u) / 1000u);
            if (left != ctx->count_shown) {
                ctx->count_shown = left;
                announce_countdown(left);
            }
        }

        if (ev == UI_POWER_UP_ENABLE) {                                               /* acelerómetro */
            if (game_try_star_power(&ctx->game_status)) uart_hal_puts("   [PLAY] star power activado\r\n");
        } else if (ev == UI_BACK) {
            enter_state(ctx, ST_PAUSE);
            break;
        }


        if (demo) {
            game_over_signal = (now > song_duration_ms(&ctx->song))
                             ? ((ctx->game_status.rock > 0) ? RESULT_WIN : RESULT_LOSE) : RESULT_NONE;
        } else {
            game_over_signal = (now >= AVANCE_PLAY_MS)
                             ? ((ctx->game_status.rock > 0) ? RESULT_WIN : RESULT_LOSE) : RESULT_NONE;
            if (game_over_signal != RESULT_NONE) uart_hal_puts("   [PLAY] tiempo cumplido: fin de la partida\r\n");
        }
        if (game_over_signal != RESULT_NONE) {
            ctx->result = (u8)game_over_signal;
            enter_state(ctx, ST_GAMEOVER);
        }
        break;
    }

    /* PAUSE */
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

    /* GAMEOVER */
    case ST_GAMEOVER:
        /* Bonus un gritosube puntaje por 5seg*/
        if (!ctx->game_status.encore_done && ctx->rec_pos < 0 && (now_sys - ctx->t_state) <= ENCORE_WIN_MS) {
            if (ctx->game_status.crowd_noise >= ENCORE_LEVEL) {
                if (++ctx->encore_cnt >= ENCORE_FRAMES) {
                    ctx->game_status.score += ctx->game_status.score * ENCORE_BONUS_PCT / 100u;
                    ctx->game_status.encore_done = 1;
                    buzzer_play_sfx(&ctx->bz, SFX_ENCORE, SFX_ENCORE_N);
                    ctx->name_entry = (u8)score_qualifies(ctx, ctx->game_status.score);
                    ctx->redraw_seq++;
                }
            } else {
                ctx->encore_cnt = 0;
            }
        }

        if (ctx->name_entry) {                       /* ingreso de iniciales*/
            char *c = &ctx->name[ctx->name_pos];
            if (ev == UI_UP)        *c = (char)((*c == 'Z') ? 'A' : *c + 1);
            else if (ev == UI_DOWN) *c = (char)((*c == 'A') ? 'Z' : *c - 1);
            else if (ev == UI_OK) {
                if (++ctx->name_pos >= 3u) {
                    ctx->rec_pos = (s8)records_insert(ctx->rec, &ctx->n_rec, ctx->name, ctx->game_status.score);
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
