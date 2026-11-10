/*
 * arrows.h - Sprites 18x18 de las flechas (notas y receptores) de Sep Hero.
 *
 * ARCHIVO GENERADO por tools/gen_arrows.py: no editar a mano.
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio.
 *
 * Son máscaras RGB565: 0x0000 = transparente (fondo), 0xFFFF = cuerpo, 0x8410 = borde.
 * render.c las colorea al dibujar. Primer índice = Lane (IZQ, ARRIBA, ABAJO, DER).
 * Quedan en flash (const). Cubre parte de AC2 (arreglos bidimensionales).
 */
#ifndef ARROWS_H
#define ARROWS_H

#include "game_types.h"

#define ARROW_W 18u
#define ARROW_H 18u
#define ARROW_BODY 0xFFFFu
#define ARROW_EDGE 0x8410u

extern const u16 arrow_spr[N_LANES][ARROW_W * ARROW_H];   /* notas (rellenas)      */
extern const u16 arrow_rcv[N_LANES][ARROW_W * ARROW_H];   /* receptores (contorno) */

#endif /* ARROWS_H */
