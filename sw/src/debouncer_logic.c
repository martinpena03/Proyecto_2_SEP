/*
 * debouncer_logic.c - Validación y valores por defecto de la configuración (AO3).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §8.2).
 *
 * Portable (sin headers de Xilinx): lo usan el driver del IP (debouncer_axi.c), el
 * lector de la consola (uart_config.c) y los tests del PC.
 */
#include "interfaces.h"

/* Valores por defecto: se aplican en el arranque para que el juego funcione sin consola. */
void debcfg_default(DebCfg *c)
{
    if (c == NULL) return;
    c->deb_ms       = 20u;              /* antirrebote típico de un pulsador          */
    c->btn_mask     = 0x7u;             /* S1, S2 y JOY_SEL habilitados               */
    c->umbral_shake = 800u;             /* |a| - 1 g > 0,8 g cuenta como sacudida     */
    c->led_pattern  = 0xFu;             /* modo claro: los 4 LEDs encendidos          */
}

/* Comprueba todos los rangos. Devuelve 0 si la configuración es válida o un código
 * negativo que identifica el primer parámetro inválido. */
int debcfg_validate(const DebCfg *c)
{
    if (c == NULL)                                         return -1;
    if (c->deb_ms < DEB_MS_MIN || c->deb_ms > DEB_MS_MAX)  return -2;
    if (c->btn_mask < 1u || c->btn_mask > 0x7u)            return -3;
    /* Hace falta S1 (bit 0): es el único OK de los menús (JOY_SEL baja el cursor). */
    if ((c->btn_mask & 0x1u) == 0u)                        return -4;
    if (c->umbral_shake < SHAKE_MG_MIN || c->umbral_shake > SHAKE_MG_MAX)   return -6;
    if (c->led_pattern < LED_PAT_MIN || c->led_pattern > LED_PAT_MAX)       return -7;
    return 0;
}
