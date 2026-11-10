/*
 * render.c - Pantallas del juego en el LCD 128x128 (Screen Manager, AO1).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (SepHero_BD.pdf página 4 = mockup, docs/MANUAL_IMPLEMENTACION.md §9.5).
 *
 * Portable: dibuja solo con lcd_fill, lcd_blit (lcd_fast.c) y text_draw (text.c). En el
 * PC esas funciones escriben en un framebuffer (sim/), así las pantallas se revisan sin
 * la placa.
 *
 * Estrategia (el SPI del LCD es lento: pintar la pantalla completa tarda ~80 ms):
 *   - Redibujo completo solo cuando cambia el estado, el tema o ctx->redraw_seq.
 *   - En el juego, cada frame se repintan únicamente las zonas que cambiaron: el tramo
 *     de carril que recorrió cada nota, los receptores que destellan y los elementos
 *     del HUD cuyo valor cambió (se guarda en una caché lo último dibujado).
 *   - Cada tramo de carril se compone en un buffer (fondo + receptor + notas) y se
 *     envía con un solo lcd_blit: no hay parpadeo ni restos de la nota anterior.
 */
#include "game_fsm.h"
#include "arrows.h"
#include "text.h"

/* ------------------------------------------------------------------------ */
/* Geometría de la pantalla de juego (píxeles)                              */
/* ------------------------------------------------------------------------ */
#define TRACK_Y0   16           /* borde superior de la pista                */
#define TRACK_Y1   119          /* borde inferior (exclusivo)                */
#define TRACK_H    (TRACK_Y1 - TRACK_Y0)
#define LANE_X0    16           /* x del primer carril                       */
#define LANE_W     24           /* ancho de cada carril                      */
#define NOTE_DX    3            /* margen de la flecha dentro del carril     */
#define RCV_Y      98           /* y de los receptores (línea de golpe)      */
#define NOTE_TOP   18           /* y en que aparece una nota (dt = lookahead) */
#define BAR_YT     21           /* interior de las barras verticales         */
#define BAR_YB     118          /* (exclusivo)                               */
#define BAR_H      (BAR_YB - BAR_YT)
#define PROG_X0    16
#define PROG_W     96
#define MAX_VIS    24           /* notas visibles a la vez                   */

/* ------------------------------------------------------------------------ */
/* Colores RGB565                                                           */
/* ------------------------------------------------------------------------ */
#define C_BLACK   0x0000u
#define C_WHITE   0xFFFFu
#define C_GRAY    0x8410u
#define C_DGRAY   0x4208u
#define C_LGRAY   0xC618u
#define C_RED     0xF800u
#define C_GREEN   0x07E0u
#define C_DGREEN  0x0480u
#define C_YELLOW  0xFFE0u
#define C_ORANGE  0xFD20u
#define C_MAGENTA 0xF81Fu
#define C_STAR    0x341Fu       /* azul del star power */

/* Paleta por tema (lo decide el sensor de luz OPT3001). Índice = Theme. */
typedef struct {
    u16 bg;        /* fondo                              */
    u16 fg;        /* texto y contornos                  */
    u16 dim;       /* texto secundario                   */
    u16 sep;       /* separadores de carril              */
    u16 border;    /* bordes de la pista                 */
    u16 rcv;       /* contorno de los receptores         */
    u16 off;       /* elementos apagados del HUD         */
    u16 hit;       /* destello de acierto                */
} Palette;

static const Palette PAL[2] = {
    /* THEME_LIGHT: mucha luz -> fondo blanco */
    { C_WHITE, C_BLACK, C_GRAY, C_LGRAY, C_DGRAY, C_DGRAY, C_LGRAY, C_DGREEN },
    /* THEME_DARK: poca luz -> fondo negro */
    { C_BLACK, C_WHITE, C_GRAY, 0x2104u, C_GRAY,  C_LGRAY, C_DGRAY, C_WHITE  },
};

/* Color de cada carril: magenta <-, verde ^, amarillo v, rojo ->. */
static const u16 LANE_COLOR[N_LANES] = { C_MAGENTA, C_GREEN, C_YELLOW, C_RED };

/* ------------------------------------------------------------------------ */
/* Caché de lo último dibujado                                              */
/* ------------------------------------------------------------------------ */
typedef struct { u16 idx; s16 y; u8 lane; } Vis;    /* nota visible y su posición */

static struct {
    u8  valid, seq, st, theme;
    /* juego */
    u32 score;
    u8  star_on, crowd_lvl, count, fx[N_LANES];
    s16 pow_h, rock_h, prog_w;
    u16 pow_col, rock_col;
    Vis vis[MAX_VIS];
    u8  nvis;
    /* menús */
    u8  cursor, name_pos, msg;
    char name[4];
} rc;

static u16 frame_no;                                 /* contador de frames (parpadeos) */
static u16 lanebuf[ARROW_W * TRACK_H];               /* composición de un tramo de carril */

/* ------------------------------------------------------------------------ */
/* Primitivas                                                               */
/* ------------------------------------------------------------------------ */

/* Mitad de brillo: borde/sombra de las flechas. */
static u16 shade(u16 c) { return (u16)((c >> 1) & 0x7BEFu); }

/* Marco de 1 px (x1, y1 exclusivos). */
static void frame(u16 x0, u16 y0, u16 x1, u16 y1, u16 c)
{
    lcd_fill(x0, y0, x1, (u16)(y0 + 1u), c);
    lcd_fill(x0, (u16)(y1 - 1u), x1, y1, c);
    lcd_fill(x0, y0, (u16)(x0 + 1u), y1, c);
    lcd_fill((u16)(x1 - 1u), y0, x1, y1, c);
}

/* Dibuja un sprite-máscara de flecha coloreado sobre fondo liso. */
static void draw_arrow(u16 x, u16 y, const u16 *mask, u16 body, u16 edge, u16 bg)
{
    u16 i;
    for (i = 0; i < ARROW_W * ARROW_H; i++) {
        u16 m = mask[i];
        lanebuf[i] = (m == 0u) ? bg : ((m == ARROW_BODY) ? body : edge);
    }
    lcd_blit(x, y, ARROW_W, ARROW_H, lanebuf);
}

static void title(const Palette *p, const char *s, u16 color)
{
    text_center(5, s, &Font16, color, p->bg);
    lcd_fill(8, 24, 120, 25, p->dim);
}

static void footer(const Palette *p, const char *s)
{
    text_center(118, s, &Font8, p->dim, p->bg);
}

/* Opción de menú: barra de x0 a x1; la seleccionada va con los colores invertidos. */
static void item(const Palette *p, u16 x0, u16 x1, u16 y, const char *s, int sel)
{
    u16 bg = sel ? p->fg : p->bg;
    u16 fg = sel ? p->bg : p->fg;
    lcd_fill(x0, (u16)(y - 1u), x1, (u16)(y + 13u), bg);
    (void)text_draw((u16)(x0 + 6u), y, s, &Font12, fg, bg);
}

/* Barra vertical que se llena de abajo hacia arriba; repinta solo la diferencia. */
static void vbar(u16 x0, u16 x1, s16 *ch, u16 *ccol, s16 h, u16 col, u16 bg, int force)
{
    if (force || col != *ccol) {
        if (h < BAR_H) lcd_fill(x0, BAR_YT, x1, (u16)(BAR_YB - h), bg);
        if (h > 0)     lcd_fill(x0, (u16)(BAR_YB - h), x1, BAR_YB, col);
    } else if (h > *ch) {
        lcd_fill(x0, (u16)(BAR_YB - h), x1, (u16)(BAR_YB - *ch), col);
    } else if (h < *ch) {
        lcd_fill(x0, (u16)(BAR_YB - *ch), x1, (u16)(BAR_YB - h), bg);
    }
    *ch   = h;
    *ccol = col;
}

/* ------------------------------------------------------------------------ */
/* Pantalla de juego                                                        */
/* ------------------------------------------------------------------------ */

/* Compone y envía el tramo [y0, y1) del carril 'lane': fondo, receptor y notas. */
static void draw_lane_span(const Palette *p, const GameState *gs, int star, u8 lane,
                           s16 y0, s16 y1, const Vis *vis, u8 nvis)
{
    s16 h, sy;
    u16 i, sx;
    u8  k;

    if (y0 < TRACK_Y0) y0 = TRACK_Y0;
    if (y1 > TRACK_Y1) y1 = TRACK_Y1;
    if (y1 <= y0) return;
    h = (s16)(y1 - y0);

    for (i = 0; i < (u16)(ARROW_W * h); i++) lanebuf[i] = p->bg;

    /* Receptor: contorno de la flecha del carril; destella al acertar o fallar. */
    if (y1 > RCV_Y && y0 < RCV_Y + (s16)ARROW_H) {
        u16 rcol = p->rcv;
        const u16 *m = arrow_rcv[lane];
        if (gs->rcv_fx[lane] > 0u) rcol = (gs->rcv_fx_kind[lane] == 1u) ? p->hit : C_RED;
        for (sy = 0; sy < (s16)ARROW_H; sy++) {
            s16 yy = (s16)(RCV_Y + sy);
            if (yy < y0 || yy >= y1) continue;
            for (sx = 0; sx < ARROW_W; sx++) {
                if (m[sy * (s16)ARROW_W + (s16)sx] != 0u) {
                    lanebuf[(yy - y0) * (s16)ARROW_W + (s16)sx] = rcol;
                }
            }
        }
    }

    /* Notas del carril que tocan el tramo (se dibujan encima del receptor). */
    for (k = 0; k < nvis; k++) {
        const u16 *m;
        u16 body, edge;
        if (vis[k].lane != lane) continue;
        if (vis[k].y >= y1 || vis[k].y + (s16)ARROW_H <= y0) continue;
        m    = arrow_spr[lane];
        body = star ? C_STAR : LANE_COLOR[lane];
        edge = shade(body);
        for (sy = 0; sy < (s16)ARROW_H; sy++) {
            s16 yy = (s16)(vis[k].y + sy);
            if (yy < y0 || yy >= y1) continue;
            for (sx = 0; sx < ARROW_W; sx++) {
                u16 v = m[sy * (s16)ARROW_W + (s16)sx];
                if (v != 0u) {
                    lanebuf[(yy - y0) * (s16)ARROW_W + (s16)sx] = (v == ARROW_BODY) ? body : edge;
                }
            }
        }
    }
    lcd_blit((u16)(LANE_X0 + LANE_W * lane + NOTE_DX), (u16)y0, ARROW_W, (u16)h, lanebuf);
}

/* Elementos fijos de la pantalla de juego. */
static void play_static(const Palette *p, int star)
{
    u16 bc = star ? C_STAR : p->border;
    u8 i;
    lcd_fill(0, 0, LCD_W, LCD_H, p->bg);
    lcd_fill(14, TRACK_Y0, 16, TRACK_Y1, bc);                    /* borde izquierdo */
    lcd_fill(112, TRACK_Y0, 114, TRACK_Y1, bc);                  /* borde derecho   */
    for (i = 1; i < N_LANES; i++) {                              /* separadores     */
        u16 x = (u16)(LANE_X0 + LANE_W * i);
        lcd_fill(x, TRACK_Y0, (u16)(x + 1u), TRACK_Y1, p->sep);
    }
    frame(2, 20, 11, 119, p->fg);                                /* barra de poder  */
    frame(117, 20, 126, 119, p->fg);                             /* rock meter      */
    lcd_fill(PROG_X0, 121, PROG_X0 + PROG_W, 125, p->off);       /* progreso        */
}

static void render_play(const GameCtx *ctx, int full)
{
    const GameState *gs = &ctx->gs;
    const Song *song = &ctx->song;
    const Palette *p = &PAL[gs->theme & 1u];
    u32 now = clock_song_ms();
    int star = (gs->star_phase == STAR_ACTIVE);
    int star_changed = (rc.star_on != (u8)star);
    Vis cur[MAX_VIS];
    u8  ncur = 0, lane, k, j;
    u16 i;
    char buf[8];

    /* --- fijos ---------------------------------------------------------- */
    if (full) {
        play_static(p, star);
    } else if (star_changed) {                                   /* bordes azules / grises */
        u16 bc = star ? C_STAR : p->border;
        lcd_fill(14, TRACK_Y0, 16, TRACK_Y1, bc);
        lcd_fill(112, TRACK_Y0, 114, TRACK_Y1, bc);
    }

    /* --- 1: puntaje ----------------------------------------------------- */
    if (full || gs->score != rc.score) {
        (void)fmt_pad(buf, gs->score % 1000000u, 6);
        (void)text_draw(2, 2, buf, &Font12, p->fg, p->bg);
        rc.score = gs->score;
    }

    /* --- centro del HUD: cuenta 3-2-1 o "x2" ---------------------------- */
    {
        u8 cnt = (now < LEADIN_MS) ? (u8)(3u - now / 1000u) : 0u;
        if (full || cnt != rc.count || star_changed) {
            lcd_fill(50, 0, 84, 16, p->bg);
            if (cnt > 0u) {
                buf[0] = (char)('0' + cnt);
                buf[1] = '\0';
                (void)text_draw(61, 0, buf, &Font16, p->fg, p->bg);
            } else if (star) {
                (void)text_draw(60, 2, "x2", &Font12, C_STAR, p->bg);
            }
            rc.count = cnt;
        }
    }

    /* --- 2: público (micrófono): 3 barras crecientes -------------------- */
    {
        u8 lvl = (gs->crowd >= 80u) ? 3u : (gs->crowd >= 50u) ? 2u : (gs->crowd >= 20u) ? 1u : 0u;
        if (full || lvl != rc.crowd_lvl) {
            for (k = 0; k < 3u; k++) {
                u16 x = (u16)(108u + 6u * k);
                u16 hh = (u16)(4u * (k + 1u));
                lcd_fill(x, (u16)(15u - hh), (u16)(x + 4u), 15, (k < lvl) ? C_ORANGE : p->off);
            }
            rc.crowd_lvl = lvl;
        }
    }

    /* --- 3: barra de poder ---------------------------------------------- */
    {
        s16 h = (s16)((u32)gs->star_bar * BAR_H / 100u);
        u16 col = (gs->theme == THEME_DARK) ? C_YELLOW : C_ORANGE;
        if (gs->star_phase == STAR_ACTIVE) col = C_STAR;
        else if (gs->star_phase == STAR_READY && ((frame_no >> 3) & 1u)) col = p->fg;  /* parpadea */
        vbar(3, 10, &rc.pow_h, &rc.pow_col, h, col, p->bg, full);
    }

    /* --- 4: rock meter --------------------------------------------------- */
    {
        s16 h = (s16)((s32)gs->rock * BAR_H / ROCK_MAX);
        u16 col = (gs->rock <= ROCK_MAX / 4) ? C_RED : C_GREEN;
        if (h < 0) h = 0;
        vbar(118, 125, &rc.rock_h, &rc.rock_col, h, col, p->bg, full);
    }

    /* --- 8: progreso de la canción -------------------------------------- */
    {
        u32 dur = song_duration_ms(song);
        s16 w = (s16)((now >= dur) ? PROG_W : (now * PROG_W / dur));
        if (full) rc.prog_w = 0;
        if (w > rc.prog_w) {
            lcd_fill((u16)(PROG_X0 + rc.prog_w), 121, (u16)(PROG_X0 + w), 125, p->fg);
            rc.prog_w = w;
        }
    }

    /* --- 5 y 6: notas y receptores --------------------------------------- */
    /* Lista de notas visibles en este frame. */
    for (i = gs->next_note; i < song->n && ncur < MAX_VIS; i++) {
        s32 dt = (s32)song->chart[i].t_ms - (s32)now;
        if (dt > (s32)NOTE_LOOKAHEAD_MS) break;                  /* el chart está ordenado */
        if (JUDGED_GET(gs, i)) continue;
        if (dt < -(s32)HIT_WIN_MS) continue;
        cur[ncur].idx  = i;
        cur[ncur].lane = (u8)song->chart[i].lane;
        cur[ncur].y    = (s16)(RCV_Y - dt * (RCV_Y - NOTE_TOP) / (s32)NOTE_LOOKAHEAD_MS);
        ncur++;
    }

    if (full || star_changed) {
        for (lane = 0; lane < N_LANES; lane++) {
            draw_lane_span(p, gs, star, lane, TRACK_Y0, TRACK_Y1, cur, ncur);
            rc.fx[lane] = (gs->rcv_fx[lane] > 0u) ? gs->rcv_fx_kind[lane] : 0u;
        }
    } else {
        /* Notas que desaparecieron o se movieron: repintar lo que ocupaban y lo que ocupan. */
        for (k = 0; k < rc.nvis; k++) {
            s16 y0 = rc.vis[k].y, y1 = (s16)(rc.vis[k].y + (s16)ARROW_H);
            for (j = 0; j < ncur; j++) {
                if (cur[j].idx == rc.vis[k].idx) break;
            }
            if (j < ncur) {
                if (cur[j].y == rc.vis[k].y) continue;           /* no se movió */
                if (cur[j].y < y0) y0 = cur[j].y;
                if (cur[j].y + (s16)ARROW_H > y1) y1 = (s16)(cur[j].y + (s16)ARROW_H);
            }
            draw_lane_span(p, gs, star, rc.vis[k].lane, y0, y1, cur, ncur);
        }
        /* Notas nuevas. */
        for (j = 0; j < ncur; j++) {
            for (k = 0; k < rc.nvis; k++) {
                if (cur[j].idx == rc.vis[k].idx) break;
            }
            if (k == rc.nvis) {
                draw_lane_span(p, gs, star, cur[j].lane, cur[j].y, (s16)(cur[j].y + (s16)ARROW_H), cur, ncur);
            }
        }
        /* Receptores cuyo destello empezó o terminó. */
        for (lane = 0; lane < N_LANES; lane++) {
            u8 fx = (gs->rcv_fx[lane] > 0u) ? gs->rcv_fx_kind[lane] : 0u;
            if (fx != rc.fx[lane]) {
                draw_lane_span(p, gs, star, lane, RCV_Y, (s16)(RCV_Y + (s16)ARROW_H), cur, ncur);
                rc.fx[lane] = fx;
            }
        }
    }
    for (k = 0; k < ncur; k++) rc.vis[k] = cur[k];
    rc.nvis    = ncur;
    rc.star_on = (u8)star;
}

/* ------------------------------------------------------------------------ */
/* Menús y otras pantallas                                                  */
/* ------------------------------------------------------------------------ */

static void render_init(const GameCtx *ctx, int full)
{
    const Palette *p = &PAL[ctx->gs.theme & 1u];
    u8 l;
    if (!full) return;
    lcd_fill(0, 0, LCD_W, LCD_H, p->bg);
    text_center(22, "SEP", &Font24, p->fg, p->bg);
    text_center(48, "HERO", &Font24, C_ORANGE, p->bg);
    for (l = 0; l < N_LANES; l++) {
        draw_arrow((u16)(LANE_X0 + LANE_W * l + NOTE_DX), 82, arrow_spr[l],
                   LANE_COLOR[l], shade(LANE_COLOR[l]), p->bg);
    }
    text_center(112, "IEE2463 - Proyecto 2", &Font8, p->dim, p->bg);
}

static const char *const MENU_TXT[MENU_ITEMS] = { "Jugar", "Records", "Configurar" };

static void render_menu(const GameCtx *ctx, int full)
{
    const Palette *p = &PAL[ctx->gs.theme & 1u];
    u8 i;
    if (full) {
        lcd_fill(0, 0, LCD_W, LCD_H, p->bg);
        title(p, "SEP HERO", p->fg);
        text_center(98, ctx->sd.mounted ? "microSD: lista" : "microSD: sin tarjeta", &Font8, p->dim, p->bg);
        footer(p, "Joy: mover   S1: OK");
    }
    for (i = 0; i < MENU_ITEMS; i++) {
        if (full || i == ctx->cursor || i == rc.cursor) {
            if (full || ctx->cursor != rc.cursor) {
                item(p, 14, 114, (u16)(36u + 18u * i), MENU_TXT[i], i == ctx->cursor);
            }
        }
    }
    rc.cursor = ctx->cursor;
}

static void render_records(const GameCtx *ctx, int full)
{
    const Palette *p = &PAL[ctx->gs.theme & 1u];
    char b[20];
    u8 i;
    if (!full) return;
    lcd_fill(0, 0, LCD_W, LCD_H, p->bg);
    title(p, "RECORDS", p->fg);
    for (i = 0; i < MAX_RECORDS; i++) {
        char *q = b;
        *q++ = (char)('1' + i);
        *q++ = '.';
        *q++ = ' ';
        if (i < ctx->n_rec) {
            q = fmt_str(q, ctx->rec[i].name);
            *q++ = ' ';
            q = fmt_pad(q, ctx->rec[i].score % 1000000u, 6);
        } else {
            q = fmt_str(q, "--- ------");
        }
        (void)text_draw(18, (u16)(32u + 16u * i), b, &Font12, (i == 0u) ? C_ORANGE : p->fg, p->bg);
    }
    footer(p, ctx->sd.mounted ? "S2: volver  (en la SD)" : "S2: volver  (en RAM)");
}

static void render_config(const GameCtx *ctx, int full)
{
    const Palette *p = &PAL[ctx->gs.theme & 1u];
    char b[20];
    char *q;
    if (!full) return;
    lcd_fill(0, 0, LCD_W, LCD_H, p->bg);
    title(p, "CONFIG", p->fg);
    text_center(30, "Configure por la", &Font8, p->fg, p->bg);
    text_center(40, "consola UART (115200)", &Font8, p->fg, p->bg);
    q = fmt_str(b, "DEB_MS  ");  (void)fmt_u32(q, ctx->cfg.deb_ms);
    (void)text_draw(18, 56, b, &Font12, p->fg, p->bg);
    q = fmt_str(b, "MASK    0x");  (void)fmt_hex(q, ctx->cfg.btn_mask, 1);
    (void)text_draw(18, 70, b, &Font12, p->fg, p->bg);
    q = fmt_str(b, "JOY     ");  (void)fmt_u32(q, ctx->cfg.joy_umbral);
    (void)text_draw(18, 84, b, &Font12, p->fg, p->bg);
    q = fmt_str(b, "SHAKE   ");  (void)fmt_u32(q, ctx->cfg.umbral_shake);
    (void)text_draw(18, 98, b, &Font12, p->fg, p->bg);
    footer(p, "S2: cancelar");
}

static void render_level(const GameCtx *ctx, int full)
{
    const Palette *p = &PAL[ctx->gs.theme & 1u];
    char b[24];
    char *q;
    if (!full) return;
    lcd_fill(0, 0, LCD_W, LCD_H, p->bg);
    title(p, "CANCION", p->fg);
    q = b;
    *q++ = '<'; *q++ = ' ';
    q = fmt_u32(q, (u32)ctx->song_sel + 1u);
    *q++ = '/';
    q = fmt_u32(q, N_SONGS);
    *q++ = ' '; *q++ = '>'; *q = '\0';
    text_center(30, b, &Font12, p->dim, p->bg);
    if (ctx->song_ok) {
        text_center(46, ctx->song.name, &Font12, C_ORANGE, p->bg);
        q = fmt_str(b, "BPM ");    (void)fmt_u32(q, ctx->song.bpm);
        text_center(62, b, &Font12, p->fg, p->bg);
        q = fmt_str(b, "Notas ");  (void)fmt_u32(q, ctx->song.n);
        text_center(76, b, &Font12, p->fg, p->bg);
        q = fmt_str(b, "Record "); (void)fmt_u32(q, (ctx->n_rec > 0u) ? ctx->rec[0].score : 0u);
        text_center(90, b, &Font12, p->fg, p->bg);
        footer(p, "S1: jugar   S2: volver");
    } else {
        text_center(60, "Error de carga", &Font12, C_RED, p->bg);
        footer(p, "S2: volver");
    }
}

static const char *const PAUSE_TXT[PAUSE_ITEMS] = { "Continuar", "Reiniciar", "Salir" };

static void render_pause(const GameCtx *ctx, int full)
{
    const Palette *p = &PAL[ctx->gs.theme & 1u];
    u8 i;
    if (full) {
        render_play(ctx, 1);                        /* la pista congelada queda de fondo */
        lcd_fill(22, 32, 106, 100, p->bg);
        frame(22, 32, 106, 100, p->fg);
        text_center(36, "PAUSA", &Font12, C_ORANGE, p->bg);
    }
    for (i = 0; i < PAUSE_ITEMS; i++) {
        if (full || ((i == ctx->cursor || i == rc.cursor) && ctx->cursor != rc.cursor)) {
            item(p, 26, 102, (u16)(54u + 15u * i), PAUSE_TXT[i], i == ctx->cursor);
        }
    }
    rc.cursor = ctx->cursor;
}

static void render_gameover(const GameCtx *ctx, int full)
{
    const Palette *p = &PAL[ctx->gs.theme & 1u];
    const GameState *gs = &ctx->gs;
    char b[24];
    char *q;
    u32 total = (u32)gs->hits + gs->misses;
    u32 t_in = clock_sys_ms() - ctx->t_state;
    u8  msg, i;

    if (full) {
        lcd_fill(0, 0, LCD_W, LCD_H, p->bg);
        if (ctx->result == RESULT_WIN) title(p, "VICTORIA", C_DGREEN);
        else                           title(p, "DERROTA", C_RED);
        q = fmt_str(b, "Puntos ");    (void)fmt_pad(q, gs->score % 1000000u, 6);
        (void)text_draw(8, 30, b, &Font12, p->fg, p->bg);
        q = fmt_str(b, "Aciertos ");  q = fmt_u32(q, total ? (gs->hits * 100u / total) : 0u);
        *q++ = '%'; *q = '\0';
        (void)text_draw(8, 44, b, &Font12, p->fg, p->bg);
        q = fmt_str(b, "Racha max "); (void)fmt_u32(q, gs->max_streak);
        (void)text_draw(8, 58, b, &Font12, p->fg, p->bg);
        if (ctx->name_entry) {
            (void)text_draw(8, 90, "Iniciales:", &Font8, p->dim, p->bg);
            footer(p, "Joy: letra   S1: OK");
        } else {
            if (ctx->rec_pos >= 0) {
                q = fmt_str(b, "Record #");  q = fmt_u32(q, (u32)ctx->rec_pos + 1u);
                q = fmt_str(q, "  ");        (void)fmt_str(q, ctx->rec[ctx->rec_pos].name);
                text_center(92, b, &Font12, C_ORANGE, p->bg);
            }
            footer(p, "S1: menu");
        }
    }

    /* Línea del encore: 0 = nada, 1 = "GRITA!" (parpadea), 2 = bonus conseguido. */
    if (gs->encore_done)                               msg = 2;
    else if (t_in <= ENCORE_WIN_MS && gs->score > 0u && ctx->rec_pos < 0)  msg = ((frame_no >> 3) & 1u) ? 1u : 3u;
    else                                               msg = 0;
    if (full || msg != rc.msg) {
        lcd_fill(8, 72, 120, 85, p->bg);
        if (msg == 1u)      text_center(73, "GRITA!", &Font12, C_ORANGE, p->bg);
        else if (msg == 2u) text_center(73, "ENCORE +10%", &Font12, C_ORANGE, p->bg);
        rc.msg = msg;
    }

    /* Iniciales: la letra en edición va con los colores invertidos. */
    if (ctx->name_entry) {
        for (i = 0; i < 3u; i++) {
            if (full || ctx->name[i] != rc.name[i] || ctx->name_pos != rc.name_pos) {
                int sel = (i == ctx->name_pos);
                b[0] = ctx->name[i];
                b[1] = '\0';
                (void)text_draw((u16)(66u + 14u * i), 86, b, &Font16,
                                sel ? p->bg : p->fg, sel ? p->fg : p->bg);
            }
        }
        for (i = 0; i < 4u; i++) rc.name[i] = ctx->name[i];
        rc.name_pos = ctx->name_pos;
    }
}

/* ------------------------------------------------------------------------ */
/* Punto de entrada: un frame                                               */
/* ------------------------------------------------------------------------ */
void render_frame(const GameCtx *ctx)
{
    u8 theme = (u8)(ctx->gs.theme & 1u);
    int full = !rc.valid || rc.seq != ctx->redraw_seq || rc.st != (u8)ctx->st || rc.theme != theme;

    frame_no++;
    switch (ctx->st) {
    case ST_INIT:     render_init(ctx, full);     break;
    case ST_MENU:     render_menu(ctx, full);     break;
    case ST_RECORDS:  render_records(ctx, full);  break;
    case ST_CONFIG:   render_config(ctx, full);   break;
    case ST_LEVEL:    render_level(ctx, full);    break;
    case ST_PLAY:     render_play(ctx, full);     break;
    case ST_PAUSE:    render_pause(ctx, full);    break;
    case ST_GAMEOVER: render_gameover(ctx, full); break;
    default: break;
    }
    rc.valid = 1;
    rc.seq   = ctx->redraw_seq;
    rc.st    = (u8)ctx->st;
    rc.theme = theme;
}
