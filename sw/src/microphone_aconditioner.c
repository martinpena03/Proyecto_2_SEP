/*
 * microphone_aconditioner.c - Microphone_aconditioner: audio del ADC -> Crowd_noise
 * (nivel del público, 0..100) para Game Manager.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §8.4); el ADC se lee con el driver del curso (lib/ADC.c).
 * Cubre AO1 (micrófono de la Booster).
 *
 * El micrófono se muestrea cada 2 ms en la misma ráfaga del joystick (joy_sample() llama a
 * sensors_feed_mic()). Cada 10 ms se cierra una ventana (mín, máx) y el nivel del público
 * sale del pico a pico de las últimas 8 ventanas (80 ms), suavizado.
 */
#include "sleep.h"
#include "hw.h"
#include "lib/ADC.h"

#define MIC_WINDOWS   8u            /* ventanas de 10 ms que se promedian              */
#define MIC_FULL      300u          /* pico a pico (cuentas) que equivale a 100 %      */

static u16 g_mic_min = 1023, g_mic_max = 0;          /* ventana en curso             */
static u16 g_win_min[MIC_WINDOWS], g_win_max[MIC_WINDOWS];
static u8  g_win_i;
static u16 g_mic_noise;             /* pico a pico en silencio                         */
static u16 g_mic_p2p;
static volatile u8 g_crowd_noise;

u8 sensors_crowd_noise(void) { return g_crowd_noise; }

void sensors_feed_mic(u16 microphone)
{
    if (microphone < g_mic_min) g_mic_min = microphone;
    if (microphone > g_mic_max) g_mic_max = microphone;
}

void sensors_mic_debug(u16 *mic_p2p, u16 *mic_noise)
{
    *mic_p2p = g_mic_p2p; *mic_noise = g_mic_noise;
}

/* Mide el ruido de fondo (pico a pico en silencio), antes de habilitar las interrupciones. */
void sensors_mic_init(void)
{
    static const u8 ch[1] = { ADC_CH_MIC };
    u16 v, mn = 1023, mx = 0;
    int i;

    for (i = 0; i < 64; i++) {
        adc_read_chain(ch, 1, &v);
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        usleep(1000);
    }
    g_mic_noise = (u16)(mx - mn);
    for (i = 0; i < (int)MIC_WINDOWS; i++) { g_win_min[i] = mn; g_win_max[i] = mx; }
}

/* Desde timer1 (cada 10 ms): cierra la ventana y calcula el nivel del público. */
void sensors_mic_window(void)
{
    u16 mn = 1023, mx = 0;
    u32 lvl;
    u8 i;

    g_win_min[g_win_i] = g_mic_min;
    g_win_max[g_win_i] = g_mic_max;
    g_win_i = (u8)((g_win_i + 1u) % MIC_WINDOWS);
    g_mic_min = 1023;
    g_mic_max = 0;
    for (i = 0; i < MIC_WINDOWS; i++) {
        if (g_win_min[i] < mn) mn = g_win_min[i];
        if (g_win_max[i] > mx) mx = g_win_max[i];
    }
    g_mic_p2p = (mx > mn) ? (u16)(mx - mn) : 0u;
    lvl = (g_mic_p2p > g_mic_noise) ? ((u32)(g_mic_p2p - g_mic_noise) * 100u / MIC_FULL) : 0u;
    if (lvl > 100u) lvl = 100u;
    g_crowd_noise = (u8)((3u * g_crowd_noise + lvl) / 4u);             /* suavizado exponencial */
}
