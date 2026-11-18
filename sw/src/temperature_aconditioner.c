/*
 * temperature_aconditioner.c - Sensor de temperatura TMP006 por I2C, con interrupción por
 * su pin DRDY (AO2: interrupción del sensor de temperatura).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: lectura del TMP006 de la AYU10 (I2C_BUZZER/sw/comun/I2C.c: registro 0x01 a
 *   0,03125 °C por cuenta); registros según la hoja de datos del TMP006; búsqueda del pin
 *   con la misma idea que opt3001_detect_int() de proyecto-final.
 *
 * Registros (dirección I2C 0x40):
 *   0x00 V_OBJ     voltaje de la termopila (leerlo también libera DRDY)
 *   0x01 T_AMB     temperatura del chip: T = (T_AMB >> 2) * 0,03125 °C (14 bits con signo)
 *   0x02 CONFIG    0x7500: MOD = 111 (conversión continua), CR = 010 (1 conversión/s,
 *                  4 promedios), EN = 1 (pin DRDY habilitado). Durante la búsqueda del pin
 *                  se usa 0x7100 (CR = 000, 4 conversiones/s) para que sea rápida.
 *   0xFE / 0xFF    identificadores: 0x5449 ("TI") y 0x0067
 *
 * Interrupción: DRDY es open-drain y activo en bajo; baja al terminar cada conversión y
 * sube al leer el resultado. La tarjeta de extensión no lo lleva a ningún Pmod, así que
 * se trae con un cable desde la BoosterPack (J2.11) a un pin del Pmod JA, que está libre.
 * axi_gpio_1 tiene los 8 pines de JA como candidatos (puerto SENS_INT) y
 * tmp006_detect_drdy() busca en el arranque el bit que baja al convertir y sube al leer. Después, cada
 * conversión interrumpe por irq_gpio (GIC 64): gpio_isr() solo levanta tmp_drdy y
 * temp_task(), desde main, lee la temperatura por I2C (el I2C nunca se usa en una ISR).
 * Si ningún candidato sigue a DRDY no hay interrupción de temperatura y se avisa por UART.
 *
 * Alarma: imprime la temperatura una vez al llegar a TEMP_ALERT_C (parámetro en hw.h) y
 * se rearma cuando baja de TEMP_ALERT_C - TEMP_HYST_CC / 100 (histéresis de 0,5 °C).
 */
#include "xil_printf.h"
#include "xiic.h"
#include "sleep.h"
#include "hw.h"
#include "lib/I2C.h"

#define TMP_ADDR      0x40u
#define TMP_VOBJ      0x00u
#define TMP_TAMB      0x01u
#define TMP_CONFIG    0x02u
#define TMP_MANUF     0xFEu
#define TMP_DEVID     0xFFu
#define TMP_ID_TI     0x5449u
#define TMP_CFG_RUN   0x7500u    /* continuo, 1 conversión/s, DRDY habilitado           */
#define TMP_CFG_FAST  0x7100u    /* continuo, 4 conversiones/s, DRDY (solo la búsqueda)  */
#define TMP_CFG_MASK  0x7F00u    /* bits MOD, CR y EN (el bit 7 es la bandera DRDY)      */

/* Nombres de los candidatos (orden de SENS_CAND en hw/scripts/02_build_bd.tcl). */
static const char *const SENS_CAND_NAME[SENS_CAND_N] = {
    "JA1 (N15)", "JA2 (L14)", "JA3 (K16)", "JA4 (K14)",
    "JA7 (N16)", "JA8 (L15)", "JA9 (J16)", "JA10 (J14)" };

static int g_tmp_ok;
static u8  g_alerted;
static s32 g_last_cc;

const char *sens_cand_name(u8 bit)
{
    return (bit < SENS_CAND_N) ? SENS_CAND_NAME[bit] : "ninguno";
}

static u16 tmp_read_reg(u8 reg)
{
    u8 w = reg;
    u8 r[2] = { 0u, 0u };
    XIic_Send(iic.BaseAddress, TMP_ADDR, &w, 1, XIIC_REPEATED_START);
    XIic_Recv(iic.BaseAddress, TMP_ADDR, r, 2, XIIC_STOP);
    return (u16)((r[0] << 8) | r[1]);
}

static int tmp_write_reg(u8 reg, u16 val)
{
    u8 w[3] = { reg, (u8)(val >> 8), (u8)(val & 0xFFu) };
    return (XIic_Send(iic.BaseAddress, TMP_ADDR, w, 3, XIIC_STOP) == 3) ? 0 : -1;
}

/* xil_printf no imprime float: centésimas de °C -> "26.03". */
static void print_cc(s32 cc)
{
    if (cc < 0) { xil_printf("-"); cc = -cc; }
    xil_printf("%d.%02d", (int)(cc / 100), (int)(cc % 100));
}


s32 tmp006_read_cc(void)
{
    s16 raw = (s16)tmp_read_reg(TMP_TAMB);
    (void)tmp_read_reg(TMP_VOBJ);
    g_last_cc = ((s32)(raw >> 2) * 3125) / 1000;
    return g_last_cc;
}

s32 tmp006_last_cc(void) { return g_last_cc; }

int tmp006_init(void)
{
    u16 man, dev, cfg;

    (void)tmp_read_reg(TMP_MANUF);       /* la primera lectura tras init_IIC() no vale (AYU10) */
    man = tmp_read_reg(TMP_MANUF);
    dev = tmp_read_reg(TMP_DEVID);
    xil_printf("[TMP] TMP006 fabricante=0x%04x dispositivo=0x%04x\r\n", man, dev);
    if (man != TMP_ID_TI) { g_tmp_ok = 0; return -1; }

    (void)tmp_write_reg(TMP_CONFIG, TMP_CFG_RUN);
    cfg = tmp_read_reg(TMP_CONFIG);
    xil_printf("[TMP] CONFIG escrito=0x%04x releido=0x%04x\r\n", TMP_CFG_RUN, cfg);
    if ((cfg & TMP_CFG_MASK) != (TMP_CFG_RUN & TMP_CFG_MASK)) { g_tmp_ok = 0; return -2; }

    g_tmp_ok = 1;
    (void)tmp006_read_cc();              /* deja DRDY liberado */
    xil_printf("[TMP] T = "); print_cc(g_last_cc);
    xil_printf(" C  (aviso al llegar a %d C, parametro TEMP_ALERT_C en hw.h)\r\n", TEMP_ALERT_C);
    return 0;
}

/* Busca el bit de axi_gpio_1 que sigue a DRDY: en 0 al terminar una conversión y en 1
 * justo después de leer el resultado, en 3 ciclos seguidos. skip_bit es un bit ya usado
 * por otro sensor (o SENS_BIT_NONE). Devuelve el bit o SENS_BIT_NONE. */
u8 tmp006_detect_drdy(u8 skip_bit)
{
    const u32 mask = (1u << SENS_CAND_N) - 1u;
    u32 low = mask, high = mask, v;
    u8 i, bit = SENS_BIT_NONE;

    if (!g_tmp_ok) return SENS_BIT_NONE;
    (void)tmp_write_reg(TMP_CONFIG, TMP_CFG_FAST);
    (void)tmp006_read_cc();
    for (i = 0; i < 3u; i++) {
        usleep(300000);                  /* a 4 conversiones/s ya terminó una: DRDY en 0 */
        v = gpio1_read() & mask;
        low &= ~v;
        (void)tmp006_read_cc();          /* leer libera DRDY */
        usleep(2000);
        v = gpio1_read() & mask;
        high &= v;
    }
    (void)tmp_write_reg(TMP_CONFIG, TMP_CFG_RUN);
    (void)tmp006_read_cc();

    low &= high;
    if (skip_bit < SENS_CAND_N) low &= ~(1u << skip_bit);
    for (i = 0; i < SENS_CAND_N; i++) {
        if ((low >> i) & 1u) { bit = i; break; }
    }
    xil_printf("[TMP] busqueda del DRDY: candidatos=0x%02x -> %s\r\n", (int)low, sens_cand_name(bit));
    if (bit == SENS_BIT_NONE)
        xil_printf("[TMP] AVISO: ningun pin de JA sigue al DRDY (falta el cable J2.11 -> JA1): sin interrupcion de temperatura\r\n");
    return bit;
}

/* Desde main, en cada vuelta. Atiende la interrupción del DRDY (tmp_drdy, que levanta
 * gpio_isr). Si el pin quedó en 0 sin que se viera el flanco (por ejemplo, una conversión
 * que terminó antes de habilitar el GIC), también lo atiende, porque el pin no vuelve a
 * subir hasta que se lee el resultado. */
void temp_task(void)
{
    s32 t, lim;

    if (!g_tmp_ok || g_tmp_bit == SENS_BIT_NONE) return;
    if (!tmp_drdy && ((gpio1_read() >> g_tmp_bit) & 1u) != 0u) return;
    tmp_drdy = 0;

    t   = tmp006_read_cc();
    lim = (s32)TEMP_ALERT_C * 100;
    if (!g_alerted && t >= lim) {
        g_alerted = 1;
        xil_printf("\r\n*** [AO2] Temperatura = "); print_cc(t);
        xil_printf(" C (llego a %d C) ***  fuente: DRDY del TMP006 -> axi_gpio_1 (GIC 64), irq #%d\r\n",
                   TEMP_ALERT_C, (int)g_tmp_irq);
    } else if (g_alerted && t < lim - (s32)TEMP_HYST_CC) {
        g_alerted = 0;                   /* bajó de 25,5 °C: el próximo cruce se vuelve a avisar */
    }
}
