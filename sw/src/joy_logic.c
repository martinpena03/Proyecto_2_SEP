/*
 * joy_logic.c - Detector de dirección del joystick (Joystick_aconditioner).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (SepHero_BD.pdf página 4 y docs/MANUAL_IMPLEMENTACION.md §8.3).
 *
 * Portable: se prueba en el PC (tests/test_joy.c). En la placa lo llama joy_sample()
 * desde la interrupción de axi_timer_1 cada 2 ms (input.c).
 *
 * Reglas:
 *   - dx = x - x0, dy = y - y0 (ADC de 10 bits, centro calibrado).
 *   - Si max(|dx|, |dy|) > umbral, la dirección es la del eje dominante (sin diagonales).
 *   - Solo cuenta el paso CENTRO -> dirección. Para repetir la misma flecha hay que
 *     volver bajo (umbral - JOY_HIST): histéresis contra el ruido del ADC.
 */
#include "interfaces.h"

void joy_det_init(JoyDet *d, u16 x0, u16 y0, u16 umbral, u8 inv_y)
{
    d->ph     = JS_CENTER;
    d->dir    = IZQ;
    d->umbral = umbral;
    d->x0     = (s16)x0;
    d->y0     = (s16)y0;
    d->inv_y  = inv_y;
}

/* Devuelve la dirección recién activada (IZQ..DER) o -1 si no hay evento nuevo. */
int joy_detect(JoyDet *d, u16 x, u16 y)
{
    s16 dx = (s16)((s16)x - d->x0);
    s16 dy = (s16)((s16)y - d->y0);
    u16 ax, ay, mag;

    if (d->inv_y) dy = (s16)-dy;          /* convención: dy > 0 = palanca hacia arriba */
    ax  = (u16)(dx < 0 ? -dx : dx);
    ay  = (u16)(dy < 0 ? -dy : dy);
    mag = (ax > ay) ? ax : ay;

    if (d->ph == JS_CENTER) {
        if (mag > d->umbral) {
            if (ax >= ay) d->dir = (dx < 0) ? IZQ : DER;       /* eje X dominante */
            else          d->dir = (dy > 0) ? ARRIBA : ABAJO;  /* eje Y dominante */
            d->ph = JS_HELD;
            return (int)d->dir;
        }
    } else if ((s16)mag < (s16)d->umbral - (s16)JOY_HIST) {
        d->ph = JS_CENTER;                /* volvió al centro: se rearma */
    }
    return -1;
}
