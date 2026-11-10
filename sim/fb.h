/*
 * fb.h - Framebuffer del PC que reemplaza al LCD (simulador y tests).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §12.2).
 */
#ifndef FB_H
#define FB_H

#include "interfaces.h"

extern u16 fb[LCD_H][LCD_W];        /* imagen actual en RGB565                      */
extern u32 fb_oob;                  /* dibujos fuera de la pantalla (deben ser 0)   */
extern u32 fb_pixels;               /* píxeles enviados desde el último fb_reset    */
extern u32 fb_calls;                /* llamadas a lcd_fill / lcd_blit               */

void fb_clear(u16 color);           /* pinta todo y pone los contadores en 0        */
void fb_reset_stats(void);
int  fb_write_ppm(const char *path);/* 0 = OK                                       */

#endif /* FB_H */
