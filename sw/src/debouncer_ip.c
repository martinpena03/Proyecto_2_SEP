#include "xil_io.h"
#include "xparameters.h"
#include "interfaces.h"

#define DEB_BASE      XPAR_DEBOUNCER_IP_BASEADDR
#define DEB_CTRL      0x00u
#define DEB_DEB_MS    0x04u
#define DEB_BTNMASK   0x08u
#define DEB_STATE     0x0Cu
#define DEB_EDGE      0x10u
#define DEB_LEDCTRL   0x14u       /* leds para el modo claro*/
#define LED_THEME     (1u << 4)
#define CTRL_EN       (1u << 0)
#define CTRL_IRQ_EN   (1u << 1)
#define CTRL_INV      (1u << 2)
#define BTN_ACTIVE_LOW 1

#define DEB_WR(d, off, v)  Xil_Out32((d)->base + (off), (u32)(v))
#define DEB_RD(d, off)     Xil_In32((d)->base + (off))

/* Inicializa y configura el hardware a partir de una configuración validada.
 * dev: instancia que se llena (base AXI y copia de la cfg). cfg: parámetros de la UART.
 * Retorna 0 = OK, -1 = puntero nulo, -2 = parámetros fuera de rango, -3 = read-back distinto. */
int debouncer_init(Debouncer *dev, const DebCfg *cfg)
{
    if (dev == NULL || cfg == NULL) return -1;
    if (debcfg_validate(cfg) != 0) return -2;
    dev->base = DEB_BASE;
    dev->cfg  = *cfg;
    /* dev->light no se toca por consola, se ve segun la luz nomas  */
    DEB_WR(dev, DEB_CTRL, 0);                           /* detener mientras se configura */
    DEB_WR(dev, DEB_DEB_MS, cfg->deb_ms);
    DEB_WR(dev, DEB_BTNMASK, cfg->btn_mask & 0x7u);
    DEB_WR(dev, DEB_EDGE, 0x7u);                        /* limpiar flancos viejos (W1C) */
    DEB_WR(dev, DEB_LEDCTRL, (cfg->led_pattern & 0xFu) | (dev->light ? LED_THEME : 0u));
    DEB_WR(dev, DEB_CTRL, CTRL_EN | CTRL_IRQ_EN | (BTN_ACTIVE_LOW ? CTRL_INV : 0u));
    if ((DEB_RD(dev, DEB_DEB_MS) & 0xFFu) != cfg->deb_ms ||
        (DEB_RD(dev, DEB_BTNMASK) & 0x7u) != (cfg->btn_mask & 0x7u) ||
        (DEB_RD(dev, DEB_LEDCTRL) & 0xFu) != (cfg->led_pattern & 0xFu)) return -3;
    return 0;
}

/* Habilita (light = 1) o deshabilita el modo claro,
 * lo llama la máquina de estados cuando la interrupcion del sensor de luz cambia
 * el tema; más adelante el mismo bit elegirá la paleta clara del LCD. */
void debouncer_set_theme(Debouncer *dev, int light)
{
    if (dev == NULL || dev->base == 0u) return;
    dev->light = (u8)(light != 0);
    DEB_WR(dev, DEB_LEDCTRL, (dev->cfg.led_pattern & 0xFu) | (dev->light ? LED_THEME : 0u));
}

u8 debouncer_led_readback(const Debouncer *dev)
{
    return (u8)(DEB_RD(dev, DEB_LEDCTRL) & 0x1Fu);
}

/* Lee las pulsaciones pendientes y las borra (W1C). La relectura asegura que el borrado
 * se completó antes de salir de la ISR (evita una interrupción de nivel "fantasma"). */
u8 debouncer_get_edges(Debouncer *dev)
{
    u8 e = (u8)(DEB_RD(dev, DEB_EDGE) & 0x7u);
    if (e) {
        DEB_WR(dev, DEB_EDGE, e);
        (void)DEB_RD(dev, DEB_EDGE);
    }
    return e;
}

u8 debouncer_get_state(const Debouncer *dev)
{
    return (u8)(DEB_RD(dev, DEB_STATE) & 0x7u);
}

/* Relee del hardware lo que quedó configurado (validación que se imprime por UART). */
void debouncer_readback(const Debouncer *dev, u16 *deb_ms, u8 *btn_mask)
{
    *deb_ms   = (u16)(DEB_RD(dev, DEB_DEB_MS) & 0xFFu);
    *btn_mask = (u8)(DEB_RD(dev, DEB_BTNMASK) & 0x7u);
}
