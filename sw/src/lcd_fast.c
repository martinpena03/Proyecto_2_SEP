/*
 * lcd_fast.c - Rellenos y bloques de píxeles por ráfagas SPI (Screen Manager, AO1).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: usa la inicialización, el SPI y el GPIO del driver LCD del curso
 *   (lib/LCD_Driver.c, LCD_SPI.c: AYU11 / LAB10). Los comandos de ventana (0x2A, 0x2B,
 *   0x2C) son los de LCD_SetWindows del curso. Lo propio es el envío: el driver del curso
 *   manda cada byte con una XSpi_Transfer aparte (pintar la pantalla tarda segundos);
 *   aquí los píxeles se acumulan en un buffer y se mandan en ráfagas escribiendo
 *   directamente los registros del AXI Quad SPI (macros de xspi_l.h), de a FIFO_DEPTH
 *   bytes. Se probó primero XSpi_Transfer con bloques grandes, como sugiere el manual
 *   §9.5.4, pero en modo polled no termina con bloques mayores que la FIFO de este core.
 *
 * El SPI del LCD se usa SOLO desde main (nunca desde una ISR).
 */
#include "xspi_l.h"
#include "hw.h"
#include "lib/LCD_Driver.h"

#define FIFO_DEPTH 16u                    /* C_FIFO_DEPTH de axi_quad_spi_0 */
#define SPI_WAIT   200000u                /* tope de espera (evita un cuelgue si el core no responde) */

#define BURST 1024u                       /* bytes por transferencia */

static u8 txbuf[BURST];
static u8 g_xoff, g_yoff;                 /* desplazamiento del área visible en la RAM del LCD */

void lcd_fast_init(void)
{
    g_xoff = (u8)sLCD_DIS.LCD_X_Adjust;   /* los que calculó LCD_Init() del curso */
    g_yoff = (u8)sLCD_DIS.LCD_Y_Adjust;
}

void lcd_set_offsets(u8 xoff, u8 yoff) { g_xoff = xoff; g_yoff = yoff; }
void lcd_get_offsets(u8 *xoff, u8 *yoff) { *xoff = g_xoff; *yoff = g_yoff; }

static void cmd(u8 c)
{
    DC_LOW();
    XSpi_Transfer(&SpiInstance, &c, NULL, 1);
}

/* Envía n bytes de datos al LCD por bloques de FIFO_DEPTH, sin pasar por XSpi_Transfer.
 * Por cada bloque: llena la FIFO de transmisión y lee (descarta) la misma cantidad de
 * bytes recibidos. El chip select queda activo durante toda la ráfaga. */
static void spi_burst(const u8 *d, u32 n)
{
    const UINTPTR base = SpiInstance.BaseAddr;
    u32 cr = XSpi_ReadReg(base, XSP_CR_OFFSET);
    u32 i, k, w, t;

    XSpi_WriteReg(base, XSP_SSR_OFFSET, ~0x1u);                         /* CS en bajo   */
    XSpi_WriteReg(base, XSP_CR_OFFSET, cr & ~XSP_CR_TRANS_INHIBIT_MASK); /* habilita TX  */
    for (i = 0; i < n; i += k) {
        k = (n - i < FIFO_DEPTH) ? (n - i) : FIFO_DEPTH;
        for (w = 0; w < k; w++) XSpi_WriteReg(base, XSP_DTR_OFFSET, d[i + w]);
        /* Cada byte enviado produce un byte recibido: leer k bytes garantiza que el
         * bloque terminó de salir por el cable (no solo que la FIFO se vació). */
        for (w = 0, t = 0; w < k && t < SPI_WAIT; t++) {
            if ((XSpi_ReadReg(base, XSP_SR_OFFSET) & XSP_SR_RX_EMPTY_MASK) == 0u) {
                (void)XSpi_ReadReg(base, XSP_DRR_OFFSET);
                w++;
            }
        }
    }
    XSpi_WriteReg(base, XSP_CR_OFFSET, cr | XSP_CR_TRANS_INHIBIT_MASK);  /* como lo deja el driver */
    XSpi_WriteReg(base, XSP_SSR_OFFSET, ~0x0u);                          /* CS en alto   */
}

static void data(const u8 *d, u32 n)
{
    DC_HIGH();
    spi_burst(d, n);
}

/* Fija la ventana [x0, x1) x [y0, y1) y deja el LCD esperando píxeles (RAMWR). */
static void window(u16 x0, u16 y0, u16 x1, u16 y1)
{
    u8 a[4];
    cmd(0x2A);
    a[0] = 0; a[1] = (u8)(x0 + g_xoff); a[2] = 0; a[3] = (u8)(x1 - 1u + g_xoff);
    data(a, 4);
    cmd(0x2B);
    a[0] = 0; a[1] = (u8)(y0 + g_yoff); a[2] = 0; a[3] = (u8)(y1 - 1u + g_yoff);
    data(a, 4);
    cmd(0x2C);
}

/* Rectángulo de un color. x1 e y1 son exclusivos. */
void lcd_fill(u16 x0, u16 y0, u16 x1, u16 y1, u16 color)
{
    u32 n, i, chunk;
    if (x1 > LCD_W) x1 = LCD_W;
    if (y1 > LCD_H) y1 = LCD_H;
    if (x0 >= x1 || y0 >= y1) return;
    n = (u32)(x1 - x0) * (u32)(y1 - y0) * 2u;             /* bytes a enviar */
    chunk = (n < BURST) ? n : BURST;
    for (i = 0; i < chunk; i += 2u) {                      /* RGB565, byte alto primero */
        txbuf[i]      = (u8)(color >> 8);
        txbuf[i + 1u] = (u8)(color & 0xFFu);
    }
    window(x0, y0, x1, y1);
    DC_HIGH();
    while (n > 0u) {
        chunk = (n < BURST) ? n : BURST;
        spi_burst(txbuf, chunk);
        n -= chunk;
    }
}

/* Bloque de w x h píxeles RGB565 (fila a fila). */
void lcd_blit(u16 x, u16 y, u16 w, u16 h, const u16 *pix)
{
    u32 n = (u32)w * h, i, k = 0;
    if (w == 0u || h == 0u || (u32)x + w > LCD_W || (u32)y + h > LCD_H) return;
    window(x, y, (u16)(x + w), (u16)(y + h));
    DC_HIGH();
    for (i = 0; i < n; i++) {
        txbuf[k++] = (u8)(pix[i] >> 8);
        txbuf[k++] = (u8)(pix[i] & 0xFFu);
        if (k >= BURST) {
            spi_burst(txbuf, k);
            k = 0;
        }
    }
    if (k > 0u) spi_burst(txbuf, k);
}
