/*
 * stubs.h - Control de los reemplazos del hardware en el PC (tests y simulador).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio.
 */
#ifndef STUBS_H
#define STUBS_H

#include "interfaces.h"

/* Estado observable / inyectable por los tests. */
extern u32   stub_sys_ms, stub_song_ms;      /* relojes                                 */
extern int   stub_song_running;
extern int   stub_play_mode;                 /* último input_set_play_mode()            */
extern int   stub_flush_count;
extern u16   stub_shake_mg;                 /* último umbral de movimiento aplicado    */
extern u8    stub_crowd;                     /* valor que devuelve sensors_crowd_level  */
extern Theme stub_theme;                     /* valor que devuelve theme_current        */
extern u16   stub_tone_f;                    /* última frecuencia pedida al buzzer      */
extern u32   stub_tone_calls;
extern int   stub_deb_inits;                 /* llamadas válidas a debouncer_init       */
extern DebCfg stub_deb_cfg;                  /* última configuración escrita al "IP"    */
extern int   stub_sd_mounted;                /* lo que responde sd_init                 */
extern int   stub_sd_saves;                  /* llamadas a sd_save_records              */
extern char  stub_sd_records[256];           /* contenido simulado de RECORDS.TXT       */
extern char  stub_uart_out[4096];            /* todo lo impreso por uart_hal_puts       */

void stub_reset(void);                       /* deja todo en su valor inicial           */
void stub_advance(u32 ms);                   /* avanza sys_ms (y song_ms si corre)      */
void stub_push_hit(u8 lane, u32 song_ms);    /* encola un golpe del joystick            */
void stub_uart_feed(const char *s);          /* caracteres que "escribe" el usuario     */

#endif /* STUBS_H */
