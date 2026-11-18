/*
 * screen_manager.c - Screen Manager, VERSION AVANCE: el LCD solo muestra la flecha que
 * hay que tocar en el nivel Demo.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio (Notas a
 * desplegar, Diagrama_de_bloques_02_01).
 *
 * Portable: dibuja solo con lcd_fill (screen_manager_lcd.c) y text_draw
 * (screen_manager_text.c). Se prueba en el PC con los stubs de tests/stubs.c.
 *
 * Comportamiento: la pantalla está en negro. En PLAY del nivel Demo, el nombre de la
 * flecha ("UP", "DOWN", "LEFT", "RIGHT") aparece en blanco durante DEMO_KEY_MS (500 ms),
 * centrado en el instante de la nota: desde 250 ms antes hasta 250 ms después. No hay
 * menú, pista, HUD ni otros elementos de la versión final.
 */
#include "state_machine.h"
#include "screen_manager_text.h"

#define C_BLACK   0x0000u
#define C_WHITE   0xFFFFu
#define KEY_NONE  0xFFu                  /* ninguna flecha en pantalla                 */
#define KEY_Y     52u                    /* fila del texto (Font24 mide 24 px de alto) */

static u8 g_key_shown = KEY_NONE;        /* flecha dibujada ahora                      */
static u8 g_lcd_ready;                   /* 1 = la pantalla ya se puso en negro        */

/* Notas a desplegar: carril de la nota cuya ventana de 500 ms contiene a 'now', o
 * KEY_NONE. La ventana es fija: no se acorta al acertar la nota. */
static u8 nota_a_desplegar(const Song *song, u32 now)
{
    u16 i;
    for (i = 0; i < song->n; i++) {
        u32 t = song->chart[i].t_ms;
        if (t > now + DEMO_KEY_MS / 2u) break;                  /* el chart está ordenado */
        if (t + DEMO_KEY_MS / 2u >= now) return (u8)song->chart[i].lane;
    }
    return KEY_NONE;
}

void render_frame(const GameCtx *ctx)
{
    u8 key = KEY_NONE;

    if (!g_lcd_ready) {
        lcd_fill(0, 0, LCD_W, LCD_H, C_BLACK);
        g_lcd_ready = 1;
    }
    if (ctx->state == ST_PLAY && ctx->select_song == LEVEL_DEMO && ctx->song_ok) {
        key = nota_a_desplegar(&ctx->song, clock_song_ms());
    }
    if (key == g_key_shown) return;                             /* sin cambios: no se dibuja */

    lcd_fill(0, KEY_Y, LCD_W, KEY_Y + Font24.Height, C_BLACK);  /* borra la flecha anterior */
    if (key != KEY_NONE) {
        const char *s = lane_name(key);
        u16 w = text_width(s, &Font24);
        (void)text_draw((u16)((LCD_W - w) / 2u), KEY_Y, s, &Font24, C_WHITE, C_BLACK);
    }
    g_key_shown = key;
}
