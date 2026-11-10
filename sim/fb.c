/*
 * fb.c - Framebuffer del PC: implementa lcd_fill y lcd_blit sobre un arreglo en memoria.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §12.2).
 *
 * Reemplaza a lcd_fast.c en el PC. Lo usan el simulador (sim/main_sim.c), que guarda
 * las pantallas como .ppm, y los tests (tests/test_render.c), que además comprueban que
 * render.c nunca dibuja fuera de los 128x128 píxeles y cuántos píxeles envía por frame
 * (en la placa cada píxel cuesta ~5 us de SPI).
 */
#include <stdio.h>
#include "fb.h"

u16 fb[LCD_H][LCD_W];
u32 fb_oob;
u32 fb_pixels;
u32 fb_calls;

void fb_reset_stats(void)
{
    fb_oob = 0;
    fb_pixels = 0;
    fb_calls = 0;
}

void fb_clear(u16 color)
{
    u16 x, y;
    for (y = 0; y < LCD_H; y++) {
        for (x = 0; x < LCD_W; x++) fb[y][x] = color;
    }
    fb_reset_stats();
}

/* Rectángulo [x0, x1) x [y0, y1) de un color. */
void lcd_fill(u16 x0, u16 y0, u16 x1, u16 y1, u16 color)
{
    u16 x, y;
    fb_calls++;
    if (x1 > LCD_W || y1 > LCD_H || x0 > x1 || y0 > y1) {
        fb_oob++;
        return;
    }
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) fb[y][x] = color;
    }
    fb_pixels += (u32)(x1 - x0) * (u32)(y1 - y0);
}

/* Copia un bloque w x h de píxeles RGB565 (fila a fila). */
void lcd_blit(u16 x, u16 y, u16 w, u16 h, const u16 *pix)
{
    u16 i, j;
    fb_calls++;
    if ((u32)x + w > LCD_W || (u32)y + h > LCD_H) {
        fb_oob++;
        return;
    }
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) fb[y + j][x + i] = pix[(u32)j * w + i];
    }
    fb_pixels += (u32)w * h;
}

/* Guarda el framebuffer como PPM binario (P6, RGB888). */
int fb_write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    u16 x, y;
    if (!f) return -1;
    fprintf(f, "P6\n%u %u\n255\n", (unsigned)LCD_W, (unsigned)LCD_H);
    for (y = 0; y < LCD_H; y++) {
        for (x = 0; x < LCD_W; x++) {
            u16 c = fb[y][x];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)(((c >> 11) & 0x1Fu) * 255u / 31u);
            rgb[1] = (unsigned char)(((c >> 5) & 0x3Fu) * 255u / 63u);
            rgb[2] = (unsigned char)((c & 0x1Fu) * 255u / 31u);
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    return 0;
}
