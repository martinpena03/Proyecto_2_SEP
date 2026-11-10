/*
 * text.c - Texto rápido sobre el LCD con las fuentes del curso (Screen Manager, AO1).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: usa las tablas de fuentes del driver LCD del curso (fonts.h, font8..24.c,
 *   AYU11 / LAB10, originales de STMicroelectronics). El algoritmo de recorrido del
 *   glifo es el de GUI_DisChar (LCD_GUI.c del curso), pero en vez de enviar cada píxel
 *   con una transferencia SPI propia (unos 25 ms por carácter), arma el glifo completo
 *   en un buffer RGB565 y lo envía de una vez con lcd_blit (menos de 1 ms).
 *
 * Portable: en el PC, lcd_blit escribe en el framebuffer del simulador.
 */
#include "text.h"

/* Buffer del glifo más grande (Font24: 17 x 24). */
static u16 glyph[MAX_WIDTH_FONT * MAX_HEIGHT_FONT];

u16 text_width(const char *s, const sFONT *f)
{
    u16 n = 0;
    while (*s++) n++;
    return (u16)(n * f->Width);
}

u16 text_draw(u16 x, u16 y, const char *s, const sFONT *f, u16 fg, u16 bg)
{
    const u16 bpr = (u16)((f->Width + 7u) / 8u);          /* bytes por fila del glifo */

    for (; *s != '\0'; s++) {
        u8 c = (u8)*s;
        const uint8_t *g;
        u16 row, col;

        if (x + f->Width > LCD_W || y + f->Height > LCD_H) break;   /* no cabe: se corta */
        if (c < ' ' || c > '~') c = '?';                           /* fuera de la tabla */
        g = &f->table[(u32)(c - ' ') * f->Height * bpr];
        for (row = 0; row < f->Height; row++) {
            for (col = 0; col < f->Width; col++) {
                /* Bit más significativo = píxel de la izquierda. */
                u8 on = (u8)(g[row * bpr + (col >> 3)] & (0x80u >> (col & 7u)));
                glyph[row * f->Width + col] = on ? fg : bg;
            }
        }
        lcd_blit(x, y, f->Width, f->Height, glyph);
        x = (u16)(x + f->Width);
    }
    return x;
}

void text_center(u16 y, const char *s, const sFONT *f, u16 fg, u16 bg)
{
    u16 w = text_width(s, f);
    u16 x = (w < LCD_W) ? (u16)((LCD_W - w) / 2u) : 0u;
    (void)text_draw(x, y, s, f, fg, bg);
}

char *fmt_pad(char *dst, u32 v, u8 digits)
{
    u8 i;
    for (i = digits; i > 0u; i--) {
        dst[i - 1u] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    dst[digits] = '\0';
    return dst + digits;
}
