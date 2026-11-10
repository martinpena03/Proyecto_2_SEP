/*
 * sensors.c - Acelerómetro (sacudida -> star power) y micrófono (público / encore).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §8.4); el ADC se lee con el driver del curso (lib/ADC.c).
 * Cubre AO1: dos de los tres periféricos de la Booster (acelerómetro y micrófono).
 *
 * Acelerómetro (salida analógica ratiométrica, 0 g = mitad de escala = 512 cuentas):
 *   v = (ax - 512, ay - 512, az - 512). En reposo |v| = 1 g = 'cpg' cuentas (se calibra).
 *   Sacudida si |a| - 1 g > UMBRAL_SHAKE, es decir |v|^2 * 10^6 > ((1000 + mg) * cpg)^2,
 *   en 2 muestras seguidas, con 500 ms de período refractario.
 * Micrófono: se muestrea cada 2 ms; cada 10 ms se cierra una ventana (mín, máx) y el
 *   nivel del público sale del pico a pico de las últimas 8 ventanas (80 ms), suavizado.
 */
#include "sleep.h"
#include "hw.h"
#include "lib/ADC.h"

#define ACC_ZERO      512           /* cuentas a 0 g                                   */
#define MIC_WINDOWS   8u            /* ventanas de 10 ms que se promedian              */
#define MIC_FULL      300u          /* pico a pico (cuentas) que equivale a 100 %      */
#define SHAKE_REFRACT_MS  500u
#define SHAKE_MUTE_MS     300u      /* tras una sacudida se ignoran los golpes         */

static u16 g_cpg = 200;             /* cuentas por g                                   */
static u16 g_shake_mg = 800;
static u16 g_acc[3];                /* última lectura (diagnóstico)                    */
static u8  g_shake_run;             /* muestras seguidas sobre el umbral               */
static u32 g_shake_last;            /* sys_ms de la última sacudida                    */
static u32 g_shakes;

static u16 g_mic_min = 1023, g_mic_max = 0;          /* ventana en curso             */
static u16 g_win_min[MIC_WINDOWS], g_win_max[MIC_WINDOWS];
static u8  g_win_i;
static u16 g_mic_noise;             /* pico a pico en silencio                         */
static u16 g_mic_p2p;
static volatile u8 g_crowd;

/* Raíz cuadrada entera (solo en la calibración). */
static u32 isqrt32(u32 v)
{
    u32 r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b != 0u) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else            { r >>= 1; }
        b >>= 2;
    }
    return r;
}

void sensors_set_shake_threshold(u16 mg) { g_shake_mg = mg; }
u8   sensors_crowd_level(void)           { return g_crowd; }

void sensors_feed_mic(u16 s)
{
    if (s < g_mic_min) g_mic_min = s;
    if (s > g_mic_max) g_mic_max = s;
}

void sensors_get_debug(u16 *acc, u16 *cpg, u16 *mic_p2p, u16 *mic_noise, u32 *shakes)
{
    acc[0] = g_acc[0]; acc[1] = g_acc[1]; acc[2] = g_acc[2];
    *cpg = g_cpg; *mic_p2p = g_mic_p2p; *mic_noise = g_mic_noise; *shakes = g_shakes;
}

/* Calibración en reposo ("no mover la placa"), antes de habilitar las interrupciones. */
int sensors_init(void)
{
    static const u8 ch[4] = { ADC_CH_ACX, ADC_CH_ACY, ADC_CH_ACZ, ADC_CH_MIC };
    s32 sx = 0, sy = 0, sz = 0;
    u16 v[4], mn = 1023, mx = 0;
    u32 mag;
    int i;

    for (i = 0; i < 64; i++) {
        adc_read_chain(ch, 4, v);
        sx += (s32)v[0] - ACC_ZERO;
        sy += (s32)v[1] - ACC_ZERO;
        sz += (s32)v[2] - ACC_ZERO;
        if (v[3] < mn) mn = v[3];
        if (v[3] > mx) mx = v[3];
        usleep(1000);
    }
    sx /= 64; sy /= 64; sz /= 64;
    mag = isqrt32((u32)(sx * sx + sy * sy + sz * sz));
    g_acc[0] = v[0]; g_acc[1] = v[1]; g_acc[2] = v[2];
    g_mic_noise = (u16)(mx - mn);
    for (i = 0; i < (int)MIC_WINDOWS; i++) { g_win_min[i] = mn; g_win_max[i] = mx; }
    /* 1 g debería dar ~200 cuentas (660 mV/g con 3,3 V y 10 bits). Fuera de un rango
     * razonable se deja el valor nominal y se informa el error. */
    if (mag < 100u || mag > 320u) { g_cpg = 200; return -1; }
    g_cpg = (u16)mag;
    return 0;
}

/* Desde timer1 (cada 10 ms). */
void sensors_sample(void)
{
    static const u8 ch[3] = { ADC_CH_ACX, ADC_CH_ACY, ADC_CH_ACZ };
    u16 v[3], mn, mx;
    s32 x, y, z;
    u64 lhs, lim;
    u32 now = clock_sys_ms(), lvl;
    u8 i;

    /* --- sacudida --- */
    adc_read_chain(ch, 3, v);
    g_acc[0] = v[0]; g_acc[1] = v[1]; g_acc[2] = v[2];
    x = (s32)v[0] - ACC_ZERO;  y = (s32)v[1] - ACC_ZERO;  z = (s32)v[2] - ACC_ZERO;
    lhs = (u64)(u32)(x * x + y * y + z * z) * 1000000u;
    lim = (u64)(1000u + g_shake_mg) * g_cpg;
    lim = lim * lim;
    if (lhs > lim) {
        if (g_shake_run < 255u) g_shake_run++;
    } else {
        g_shake_run = 0;
    }
    if (g_shake_run >= 2u && (now - g_shake_last) >= SHAKE_REFRACT_MS) {
        g_shake_last = now;
        g_shakes++;
        ui_push(UI_SHAKE);                                 /* -> game_try_star_power() */
        input_mute_hits_until(now + SHAKE_MUTE_MS);        /* la palanca se mueve al sacudir */
    }

    /* --- micrófono: cierra la ventana de 10 ms y calcula el nivel del público --- */
    g_win_min[g_win_i] = g_mic_min;
    g_win_max[g_win_i] = g_mic_max;
    g_win_i = (u8)((g_win_i + 1u) % MIC_WINDOWS);
    g_mic_min = 1023;
    g_mic_max = 0;
    mn = 1023; mx = 0;
    for (i = 0; i < MIC_WINDOWS; i++) {
        if (g_win_min[i] < mn) mn = g_win_min[i];
        if (g_win_max[i] > mx) mx = g_win_max[i];
    }
    g_mic_p2p = (mx > mn) ? (u16)(mx - mn) : 0u;
    lvl = (g_mic_p2p > g_mic_noise) ? ((u32)(g_mic_p2p - g_mic_noise) * 100u / MIC_FULL) : 0u;
    if (lvl > 100u) lvl = 100u;
    g_crowd = (u8)((3u * g_crowd + lvl) / 4u);             /* suavizado exponencial */
}
