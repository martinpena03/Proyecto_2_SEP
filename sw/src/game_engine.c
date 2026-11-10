/*
 * game_engine.c - Reglas del juego (Game Manager, AC2).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (SepHero_BD.pdf páginas 1 y 3, docs/MANUAL_IMPLEMENTACION.md §9.1).
 *
 * Portable: no usa hardware. Se prueba en el PC (tests/test_engine.c).
 *
 * Mecánica:
 *   - Acierto: la flecha del joystick coincide con el carril de una nota y
 *     |t_golpe - t_nota| <= HIT_WIN_MS. Suma PTS_NOTA (x2 con star power) y rock +1.
 *   - Flecha equivocada con una nota en la ventana: rock -2 y la racha vuelve a 0.
 *   - Nota que pasa el receptor sin golpe: rock -2 y la racha vuelve a 0.
 *   - STAR_STREAK aciertos seguidos llenan la barra; con la barra llena, una sacudida
 *     activa el star power (STAR_MS con puntaje doble).
 *   - Rock meter en 0 => derrota. Fin de la canción => victoria.
 */
#include "interfaces.h"

/* Deja el estado listo para una partida nueva. Conserva el tema (lo fija el OPT3001). */
void game_reset(GameState *gs)
{
    u8 theme = gs->theme;
    u16 i;
    u8 *p = (u8 *)gs;
    for (i = 0; i < sizeof(*gs); i++) p[i] = 0;      /* equivalente a memset */
    gs->theme      = theme;
    gs->rock       = ROCK_START;
    gs->star_phase = STAR_CHARGING;
}

/* Suma con saturación del rock meter en [0, ROCK_MAX]. */
static void rock_add(GameState *gs, int delta)
{
    int r = (int)gs->rock + delta;
    if (r > ROCK_MAX) r = ROCK_MAX;
    if (r < 0)        r = 0;
    gs->rock = (s8)r;
}

/* Un fallo (nota perdida o flecha equivocada) corta la racha y reinicia la carga. */
static void break_streak(GameState *gs)
{
    gs->streak = 0;
    if (gs->star_phase == STAR_CHARGING) gs->charge = 0;   /* en LISTO la barra se conserva */
}

/* Juzga un golpe contra el chart. Devuelve 1 = acierto, 0 = fallo o golpe ignorado. */
int game_judge_hit(GameState *gs, const Song *song, const Hit *hit)
{
    s32 best = -1;
    u32 best_dt = HIT_WIN_MS + 1u;
    int other_in_window = 0;
    u16 i;

    if (hit->lane >= N_LANES) return 0;
    for (i = gs->next_note; i < song->n; i++) {
        const Note *nt = &song->chart[i];
        u32 dt;
        if (nt->t_ms > hit->song_ms + HIT_WIN_MS) break;       /* el chart está ordenado */
        if (JUDGED_GET(gs, i)) continue;
        dt = (nt->t_ms > hit->song_ms) ? (nt->t_ms - hit->song_ms) : (hit->song_ms - nt->t_ms);
        if (dt > HIT_WIN_MS) continue;
        if ((u8)nt->lane == hit->lane) {
            if (dt < best_dt) { best = (s32)i; best_dt = dt; } /* la más cercana */
        } else {
            other_in_window = 1;
        }
    }

    if (best >= 0) {                                           /* ACIERTO */
        JUDGED_SET(gs, (u16)best);
        gs->hits++;
        gs->streak++;
        if (gs->streak > gs->max_streak) gs->max_streak = gs->streak;
        gs->score += PTS_NOTA * ((gs->star_phase == STAR_ACTIVE) ? 2u : 1u);
        rock_add(gs, ROCK_HIT);
        if (gs->star_phase == STAR_CHARGING) {
            gs->charge++;
            if (gs->charge >= STAR_STREAK) gs->star_phase = STAR_READY;
        }
        gs->rcv_fx[hit->lane]      = 4;                        /* destello de 4 frames */
        gs->rcv_fx_kind[hit->lane] = 1;
        return 1;
    }
    if (other_in_window) {                                     /* FLECHA EQUIVOCADA */
        break_streak(gs);
        rock_add(gs, ROCK_WRONG);
        gs->rcv_fx[hit->lane]      = 4;
        gs->rcv_fx_kind[hit->lane] = 2;
        return 0;                                              /* la nota sigue disponible */
    }
    return 0;                                                  /* sin notas cerca: se ignora */
}

/* Avanza el estado al instante 'now' de la canción. Se llama una vez por frame,
 * DESPUÉS de vaciar la cola de golpes (si no, una nota podría darse por perdida antes
 * de juzgar un golpe ya registrado que le correspondía). */
void game_update(GameState *gs, const Song *song, u32 now)
{
    u16 i;
    u8 l;

    /* Notas que pasaron el receptor sin golpe. */
    for (i = gs->next_note; i < song->n; i++) {
        if (JUDGED_GET(gs, i)) continue;
        if (song->chart[i].t_ms + HIT_WIN_MS < now) {
            JUDGED_SET(gs, i);
            gs->misses++;
            break_streak(gs);
            rock_add(gs, ROCK_MISS);
            gs->rcv_fx[song->chart[i].lane]      = 4;
            gs->rcv_fx_kind[song->chart[i].lane] = 2;
        } else {
            break;
        }
    }
    while (gs->next_note < song->n && JUDGED_GET(gs, gs->next_note)) gs->next_note++;

    /* Sub-FSM del star power: ACTIVO termina cuando timer0 deja star_ms en 0. */
    if (gs->star_phase == STAR_ACTIVE && gs->star_ms == 0u) {
        gs->star_phase = STAR_CHARGING;
        gs->charge = 0;
    }
    if (gs->star_phase == STAR_CHARGING)      gs->star_bar = (u8)((u32)gs->charge * 100u / STAR_STREAK);
    else if (gs->star_phase == STAR_READY)    gs->star_bar = 100;
    else                                      gs->star_bar = (u8)(gs->star_ms * 100u / STAR_MS);

    /* Destellos de los receptores: un frame menos. */
    for (l = 0; l < N_LANES; l++) {
        if (gs->rcv_fx[l] > 0u) gs->rcv_fx[l]--;
    }
}

/* Sacudida detectada: activa el star power si la barra está llena. */
int game_try_star_power(GameState *gs)
{
    if (gs->star_phase != STAR_READY) return 0;
    gs->star_phase = STAR_ACTIVE;
    gs->star_ms    = STAR_MS;
    return 1;
}

/* Duración total de la partida: fin de la última nota más el margen final. */
u32 song_duration_ms(const Song *song)
{
    const Note *last;
    if (song->n == 0u) return SONG_TAIL_MS;
    last = &song->chart[song->n - 1u];
    return last->t_ms + last->dur + SONG_TAIL_MS;
}

/* Devuelve RESULT_LOSE si el rock meter llegó a 0, RESULT_WIN si terminó la canción
 * y RESULT_NONE si la partida sigue. */
int game_is_over(const GameState *gs, const Song *song, u32 now)
{
    if (gs->rock <= 0) return RESULT_LOSE;
    if (now > song_duration_ms(song)) return RESULT_WIN;
    return RESULT_NONE;
}
