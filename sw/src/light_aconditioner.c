/*
 * light_aconditioner.c - Sensor de luz OPT3001 por I2C: registros y ventana de comparación.
 * theme_*  - Tema claro / oscuro gobernado por la interrupción del sensor.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: acceso I2C del driver del curso (lib/I2C.c, AYU10); registros según la hoja de
 *   datos del OPT3001 (AYU10/opt3001.pdf); lógica propia (manual §8.5).
 * Cubre AO1 (tercer periférico de la Booster y modificación de 3 registros: CONFIG,
 * LOW_LIMIT, HIGH_LIMIT, con relectura). La interrupción de sensor de AO2 es la del
 * TMP006 (temperature_aconditioner.c).
 *
 * Registros (dirección I2C 0x44):
 *   0x00 RESULT      lux = 0,01 * 2^E[15:12] * R[11:0]
 *   0x01 CONFIG      se escribe 0xC410: rango automático, 100 ms, conversión continua,
 *                    L = 1 (latch, modo ventana), POL = 0 (INT activo en bajo).
 *                    Leerlo borra el latch y entrega FH (bit 6) y FL (bit 5).
 *   0x02 LOW_LIMIT   límite inferior de la ventana (mismo formato E/R)
 *   0x03 HIGH_LIMIT  límite superior
 *   0x7E / 0x7F      identificadores: 0x5449 ("TI") y 0x3001
 *
 * Interrupción: el INT del OPT3001 llegaría a axi_gpio_1 (GIC 64). Como el pin no está
 * identificado, axi_gpio_1 tiene 8 pines candidatos del Pmod JA (puerto SENS_INT) y en el
 * arranque opt3001_detect_int() alterna POL (CONFIG[3]) y busca el bit que la sigue. Si
 * ninguno lo hace (lo que pasó en las pruebas), el comparador de ventana del sensor sigue
 * funcionando por hardware y su bandera FH/FL se lee cada 200 ms (respaldo por timer).
 *
 * Histéresis: en tema CLARO la ventana es [lo, máx]: el INT se activa cuando lux < lo y
 * se pasa a OSCURO; en OSCURO la ventana es [0, hi]: el INT se activa cuando lux > hi.
 * El I2C se usa solo desde main: gpio_isr() únicamente levanta opt_pending.
 */
#include "xil_printf.h"
#include "sleep.h"
#include "hw.h"
#include "lib/I2C.h"

#define OPT_RESULT   0x00u
#define OPT_CONFIG   0x01u
#define OPT_LOW      0x02u
#define OPT_HIGH     0x03u
#define OPT_MANUF    0x7Eu
#define OPT_DEVID    0x7Fu
#define OPT_CFG_VAL  0xC410u
#define OPT_FLAG_FH  0x0040u
#define OPT_FLAG_FL  0x0020u

static Opt3001Cfg g_cfg = { 40, 80 };
static int g_sensor_ok;
static volatile Theme g_light_mode = THEME_DARK;
static u32 g_last_poll;
volatile u32 g_theme_changes;

/* Nombre del pin candidato (la tabla está en temperature_aconditioner.c: el TMP006 y el
 * OPT3001 comparten los 8 candidatos de SENS_INT). */
const char *opt_cand_name(u8 bit)
{
    return (bit < OPT_CAND_N) ? sens_cand_name(bit) : "ninguno (respaldo por timer)";
}

/* Convierte lux enteros al formato exponente/mantisa del sensor (resolución 0,01 lux). */
u16 lux_to_reg(u32 lux)
{
    u32 v = lux * 100u;
    u16 e = 0;
    while (v > 0x0FFFu && e < 11u) { v >>= 1; e++; }
    return (u16)((e << 12) | (v & 0x0FFFu));
}

static u32 reg_to_lux(u16 r)
{
    return ((u32)(r & 0x0FFFu) << (r >> 12)) / 100u;
}

u16 opt3001_read_lux(void)
{
    u32 lux = reg_to_lux(opt_read_reg(OPT_RESULT));
    return (lux > 0xFFFFu) ? 0xFFFFu : (u16)lux;
}

/* Escribe la ventana que corresponde al tema y relee ambos registros (validación AO1). */
void opt3001_arm(Theme t)
{
    u16 lo = (t == THEME_LIGHT) ? lux_to_reg(g_cfg.lo_lux) : 0x0000u;
    u16 hi = (t == THEME_LIGHT) ? 0xBFFFu : lux_to_reg(g_cfg.hi_lux);
    (void)opt_write_reg(OPT_LOW, lo);
    (void)opt_write_reg(OPT_HIGH, hi);
    xil_printf("[OPT] ventana escrita LOW=0x%04x HIGH=0x%04x | releida LOW=0x%04x HIGH=0x%04x\r\n",
               lo, hi, opt_read_reg(OPT_LOW), opt_read_reg(OPT_HIGH));
}

/* Verifica los IDs, escribe CONFIG, elige el tema inicial y arma la ventana. */
int opt3001_init(const Opt3001Cfg *cfg)
{
    u16 man, dev, light_level;
    if (cfg != NULL) g_cfg = *cfg;
    man = opt_read_reg(OPT_MANUF);
    dev = opt_read_reg(OPT_DEVID);
    xil_printf("[OPT] ID fabricante=0x%04x (0x5449) dispositivo=0x%04x (0x3001)\r\n", man, dev);
    if (man != 0x5449u || dev != 0x3001u) return -1;
    (void)opt_write_reg(OPT_CONFIG, OPT_CFG_VAL);
    xil_printf("[OPT] CONFIG escrito=0x%04x releido=0x%04x\r\n", OPT_CFG_VAL, opt_read_reg(OPT_CONFIG));
    if ((opt_read_reg(OPT_CONFIG) & 0xFE1Fu) != (OPT_CFG_VAL & 0xFE1Fu)) return -2;
    usleep(150000);                                     /* primera conversión (100 ms) */
    light_level = opt3001_read_lux();
    g_light_mode = (light_level >= (u16)((g_cfg.lo_lux + g_cfg.hi_lux) / 2u)) ? THEME_LIGHT : THEME_DARK;
    opt3001_arm(g_light_mode);
    (void)opt_read_reg(OPT_CONFIG);                     /* borra un latch pendiente */
    xil_printf("[OPT] lux=%d tema=%s\r\n", light_level, (g_light_mode == THEME_LIGHT) ? "CLARO" : "OSCURO");
    return 0;
}

/* Busca el pin del INT: con el lux dentro de la ventana no hay evento y el pin queda en
 * su nivel inactivo, que depende de POL (POL = 0: suelto -> 1 por el pull-up; POL = 1:
 * el sensor lo tira a 0). El bit que vale ~POL en las 6 muestras es el INT. Requiere
 * gpio1_init() antes. Deja CONFIG como estaba y el latch limpio. */
u8 opt3001_detect_int(void)
{
    const u32 mask = (1u << OPT_CAND_N) - 1u;
    u32 anti = mask, follow = mask, v;
    u8 i, bit = OPT_BIT_NONE;

    for (i = 0; i < 6u; i++) {
        (void)opt_write_reg(OPT_CONFIG, (i & 1u) ? (OPT_CFG_VAL | 0x0008u) : OPT_CFG_VAL);
        usleep(20000);
        v = gpio1_read() & mask;
        if (i & 1u) { anti &= ~v; follow &= v;  }        /* POL = 1 */
        else        { anti &= v;  follow &= ~v; }        /* POL = 0 */
    }
    (void)opt_write_reg(OPT_CONFIG, OPT_CFG_VAL);
    (void)opt_read_reg(OPT_CONFIG);
    if (anti == 0u) anti = follow;                       /* por si el pin llega invertido */
    for (i = 0; i < OPT_CAND_N; i++) {
        if ((anti >> i) & 1u) { bit = i; break; }
    }
    xil_printf("[OPT] busqueda del INT: candidatos=0x%02x -> %s\r\n", (int)anti, opt_cand_name(bit));
    return bit;
}

/* ------------------------------- tema -------------------------------------- */

void theme_init(int sensor_ok, const Opt3001Cfg *cfg)
{
    g_sensor_ok = sensor_ok;
    if (cfg != NULL) g_cfg = *cfg;
    if (!sensor_ok) g_light_mode = THEME_DARK;               /* sin sensor: tema fijo */
}

Theme light_mode_current(void) { return g_light_mode; }

/* Desde main, en cada vuelta: si el OPT3001 interrumpió, lee el sensor por I2C, cambia el
 * tema y rearma la ventana. Como respaldo, cada 500 ms revisa el nivel del pin por si se
 * perdió un flanco (el INT queda en bajo hasta que se lee CONFIG). Sin pin INT, cada
 * 200 ms lee las banderas FH/FL que el comparador del sensor dejó enganchadas. */
void theme_task(void)
{
    u32 now = clock_sys_ms();
    u16 cfg, light_level;
    Theme nt;
    const char *src;

    if (!g_sensor_ok) return;
    if (g_opt_bit == OPT_BIT_NONE) {
        if ((now - g_last_poll) < 200u) return;
        g_last_poll = now;
        cfg = opt_read_reg(OPT_CONFIG);                 /* borra el latch; trae FH / FL */
        if ((cfg & (OPT_FLAG_FH | OPT_FLAG_FL)) == 0u) return;
        src = "comparador OPT3001 (respaldo timer)";
    } else {
        if (!opt_pending) {
            if ((now - g_last_poll) < 500u) return;
            g_last_poll = now;
            if (((gpio1_read() >> g_opt_bit) & 1u) != 0u) return; /* INT inactivo */
        }
        opt_pending = 0;
        g_last_poll = now;
        cfg = opt_read_reg(OPT_CONFIG);                 /* borra el latch; trae FH / FL */
        src = "interrupcion INT OPT3001 (GIC 64)";
    }

    light_level = opt3001_read_lux();
    nt  = g_light_mode;
    if (cfg & OPT_FLAG_FL)      nt = THEME_DARK;        /* light_level < LOW_LIMIT  */
    else if (cfg & OPT_FLAG_FH) nt = THEME_LIGHT;       /* light_level > HIGH_LIMIT */
    else if (light_level < g_cfg.lo_lux) nt = THEME_DARK;
    else if (light_level > g_cfg.hi_lux) nt = THEME_LIGHT;

    if (nt != g_light_mode) {
        g_light_mode = nt;
        g_theme_changes++;
        if (nt == THEME_LIGHT)
            xil_printf("\r\n*** [AO2] Mucha luz *** lux=%d (> %d)  fuente: %s -> modo claro, LEDs ON\r\n",
                       light_level, g_cfg.hi_lux, src);
        else
            xil_printf("\r\n*** [AO2] Poca luz *** lux=%d (< %d)  fuente: %s -> modo oscuro, LEDs OFF\r\n",
                       light_level, g_cfg.lo_lux, src);
        opt3001_arm(nt);
    }
    (void)opt_read_reg(OPT_CONFIG);                     /* limpia eventos pendientes */
    xil_printf("[OPT] evento lux=%d CONFIG=0x%04x tema=%s\r\n", light_level, cfg,
               (g_light_mode == THEME_LIGHT) ? "CLARO" : "OSCURO");
}
