/*
 * text.h - Texto rápido sobre el LCD con las fuentes del curso.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Ver text.c.
 */
#ifndef TEXT_H
#define TEXT_H

#include "interfaces.h"
#include "lib/fonts.h"      /* sFONT, Font8..Font24 (driver LCD del curso) */

/* Dibuja la cadena con su esquina superior izquierda en (x, y). Devuelve la x siguiente. */
u16  text_draw(u16 x, u16 y, const char *s, const sFONT *f, u16 fg, u16 bg);
/* Ancho en píxeles de la cadena. */
u16  text_width(const char *s, const sFONT *f);
/* Dibuja la cadena centrada horizontalmente en la pantalla. */
void text_center(u16 y, const char *s, const sFONT *f, u16 fg, u16 bg);
/* Escribe v en decimal con 'digits' cifras y ceros a la izquierda. Devuelve el fin. */
char *fmt_pad(char *dst, u32 v, u8 digits);

#endif /* TEXT_H */
