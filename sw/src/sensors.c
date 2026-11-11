/*
 * sensors.c - Acelerómetro: evento "movimiento" desde la interrupción de timer1 (AO2).
 *             VERSION AVANCE (sin micrófono).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §8.4); el ADC se lee con el driver del curso (lib/ADC.c).
 *
 * Acelerómetro (salida analógica ratiométrica, 0 g = mitad de escala = 512 cuentas):
 *   v = (ax - 512, ay - 512, az - 512). En reposo |v| = 1 g = 'cpg' cuentas (se calibra).
 *   Movimiento si |a| - 1 g > UMBRAL_SHAKE, es decir |v|^2 * 10^6 > ((1000 + mg) * cpg)^2,
 *   en 2 muestras seguidas, con 500 ms de período refractario. El umbral lo entrega el
 *   usuario por la UART (estado CONFIG). No tiene pin de interrupción: la detección corre
 *   en la ISR de axi_timer_1 y main imprime el aviso.
 */
#include "sleep.h"
#include "hw.h"
#include "lib/ADC.h"

#define ACC_ZERO          512       /* cuentas a 0 g                                   */
#define SHAKE_REFRACT_MS  500u

static u16 g_cpg = 200;             /* cuentas por g                                   */
static u16 g_shake_mg = 800;
static u16 g_acc[3];                /* última lectura (consola)                        */
static u8  g_shake_run;             /* muestras seguidas sobre el umbral               */
static u32 g_shake_last;            /* sys_ms del último movimiento                    */
static volatile u32 g_motion_cnt;   /* lo incrementa la ISR; main imprime el aviso      */
static volatile u16 g_motion_mg;

/* Raíz cuadrada entera. */
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
u32  sensors_motion_count(void)          { return g_motion_cnt; }
u16  sensors_last_motion_mg(void)        { return g_motion_mg; }
/* Sin micrófono en el avance: el público queda en 0 (el bonus encore no se activa). */
u8   sensors_crowd_level(void)           { return 0u; }

void sensors_get_debug(u16 *acc, u16 *cpg, u32 *shakes)
{
    acc[0] = g_acc[0]; acc[1] = g_acc[1]; acc[2] = g_acc[2];
    *cpg = g_cpg; *shakes = g_motion_cnt;
}

/* Calibración en reposo ("no mover la placa"), antes de habilitar las interrupciones. */
int sensors_init(void)
{
    static const u8 ch[3] = { ADC_CH_ACX, ADC_CH_ACY, ADC_CH_ACZ };
    s32 sx = 0, sy = 0, sz = 0;
    u16 v[3];
    u32 mag;
    int i;

    for (i = 0; i < 64; i++) {
        adc_read_chain(ch, 3, v);
        sx += (s32)v[0] - ACC_ZERO;
        sy += (s32)v[1] - ACC_ZERO;
        sz += (s32)v[2] - ACC_ZERO;
        usleep(1000);
    }
    sx /= 64; sy /= 64; sz /= 64;
    mag = isqrt32((u32)(sx * sx + sy * sy + sz * sz));
    g_acc[0] = v[0]; g_acc[1] = v[1]; g_acc[2] = v[2];
    /* 1 g debería dar ~200 cuentas (660 mV/g con 3,3 V y 10 bits). Fuera de un rango
     * razonable se deja el valor nominal y se informa el error. */
    if (mag < 100u || mag > 320u) { g_cpg = 200; return -1; }
    g_cpg = (u16)mag;
    return 0;
}

/* Desde la ISR de axi_timer_1 (cada 10 ms). */
void sensors_sample(void)
{
    static const u8 ch[3] = { ADC_CH_ACX, ADC_CH_ACY, ADC_CH_ACZ };
    u16 v[3];
    s32 x, y, z;
    u64 lhs, lim;
    u32 now = clock_sys_ms();

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
        u32 a_mg = isqrt32((u32)(lhs / 1000000u)) * 1000u / g_cpg;
        g_shake_last = now;
        g_motion_mg = (u16)((a_mg > 1000u) ? (a_mg - 1000u) : 0u);
        g_motion_cnt++;                                    /* AO2: evento de movimiento */
        ui_push(UI_SHAKE);                                 /* en PLAY: star power       */
    }
}
