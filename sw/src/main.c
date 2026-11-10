/*
 * main.c - Punto de entrada de Sep Hero: inicialización y bucle principal.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: diseño propio (docs/MANUAL_IMPLEMENTACION.md §9.6). El orden de inicialización
 *   de LCD / ADC / I2C sigue el main.c del Proyecto_Base_Booster (LAB10).
 *
 * Modelo de ejecución:
 *   - Las interrupciones (isr.c) llevan el tiempo, muestrean el joystick y los sensores,
 *     secuencian el buzzer y levantan g_frame_flag cada 30 ms.
 *   - El bucle principal atiende el sensor de luz por I2C (theme_task) y, una vez por
 *     frame, avanza la máquina de estados (fsm_step) y dibuja (render_frame).
 *   - El dibujo nunca ocurre dentro de una ISR: el SPI del LCD es lento.
 */
#include "xparameters.h"
#include "xil_printf.h"
#include "xgpio.h"
#include "xtime_l.h"
#include "hw.h"
#include "lib/platform.h"
#include "lib/LCD_Driver.h"
#include "lib/LCD_GUI.h"
#include "lib/ADC.h"
#include "lib/I2C.h"

static GameCtx ctx;                 /* global: ~7 KB, fuera del stack */

/* Imprime el resultado de un paso de inicialización. */
static void report(const char *what, int code)
{
    if (code == 0) xil_printf("[INIT] %s OK\r\n", what);
    else           xil_printf("[INIT] %s FALLO (%d)\r\n", what, code);
}

/* Inicializa todo el hardware. El GIC va al final para que ninguna interrupción corra
 * con un periférico sin inicializar. */
static void init_all(GameCtx *c)
{
    static const Opt3001Cfg optcfg = { 40, 80 };     /* lux: bajo 40 oscuro, sobre 80 claro */
    int r;

    init_platform();
    r = audio_hw_init();                             /* lo primero: buzzer en silencio */
    xil_printf("\r\n=== SEP HERO === %s %s  (mudo=%d)\r\n", __DATE__, __TIME__, audio_is_muted());
    report("buzzer TTC0", r);

    /* LCD: GPIO de RS/RST + SPI + secuencia del ST7735S (driver del curso). */
    r = (XGpio_Initialize(&gpio0, XPAR_AXI_GPIO_0_DEVICE_ID) == XST_SUCCESS) ? 0 : -1;
    if (r == 0) r = (XSpi_Init(&SpiInstance, SPI_DEVICE_ID) == XST_SUCCESS) ? 0 : -2;
    if (r == 0) {
        LCD_Init(SCAN_DIR_DFT);
        lcd_fast_init();
        lcd_fill(0, 0, LCD_W, LCD_H, 0x0000);
    }
    report("LCD", r);

    /* ADC SPI: joystick (centro) y acelerómetro / micrófono (reposo). */
    r = (init_adc(&SpiInstance1, SPI_DEVICE_ID_1) == XST_SUCCESS) ? 0 : -1;
    report("ADC SPI", r);
    report("joystick (centro)", input_init());
    report("acelerometro + microfono", sensors_init());

    /* I2C + sensor de luz. Si falla, el tema queda fijo en oscuro. axi_gpio_1 va antes
     * para poder buscar en qué pin llega el INT (AO2). */
    report("GPIO OPT_INT", gpio1_init(OPT_INT_BIT));
    r = (init_IIC() == XST_SUCCESS) ? 0 : -1;
    report("I2C", r);
    if (r == 0) r = opt3001_init(&optcfg);
    report("OPT3001", r);
    theme_init(r == 0, &optcfg);
    g_opt_bit = (r == 0) ? opt3001_detect_int() : OPT_BIT_NONE;

    /* IP propio con la configuración por defecto (el juego funciona sin consola). */
    debcfg_default(&c->cfg);
    report("IP debouncer_axi", fsm_apply_cfg(c, &c->cfg));

    buzzer_init(&c->bz, 50);
    report("timers + GIC", isr_init(c));
}

/* AO2: el acelerómetro no tiene pin de interrupción (salida analógica). La detección
 * corre dentro de la ISR de axi_timer_1 (sensors_sample, cada 10 ms) y aquí solo se
 * imprime el aviso, porque la UART no se usa dentro de una interrupción. */
static void motion_task(void)
{
    static u32 seen;
    u32 n = sensors_motion_count();
    if (n != seen) {
        seen = n;
        xil_printf("\r\n*** [AO2] Movimiento detectado *** #%d  |a|-1g = %d mg (umbral %d mg)  fuente: ISR axi_timer_1\r\n",
                   (int)n, (int)sensors_last_motion_mg(), (int)ctx.cfg.umbral_shake);
    }
}

int main(void)
{
    XTime t0, t1, t2;

    init_all(&ctx);
    xil_printf("\r\n--- AVANCE: AO2 (luz + acelerometro por interrupcion), AO3 (debouncer_axi + LEDs modo claro),\r\n"
               "    AC1 (maquina de estados por consola), AC6 (melodia del menu) ---\r\n"
               "    INT de luz: %s | umbrales: oscuro < %d lux, claro > %d lux\r\n",
               opt_cand_name(g_opt_bit), 40, 80);
    fsm_start(&ctx);                             /* SD (o respaldo), récords y logo */
#ifdef SEP_DIAG
    xil_printf("[DIAG] consola de diagnostico activa: 'h' = ayuda\r\n");
#endif

    while (1) {
        theme_task();                            /* I2C del OPT3001, si hubo OPT_INT */
        motion_task();                           /* aviso del acelerómetro (AO2)     */
#ifdef SEP_DIAG
        diag_poll(&ctx);
#endif
        if (g_frame_flag) {                      /* cada 30 ms (axi_timer_1) */
            UiEvent ev;
            g_frame_flag = 0;
            ev = ui_pop();
#ifdef SEP_DIAG
            if (ev == UI_NONE) ev = diag_pop_event();
#endif
            XTime_GetTime(&t0);
            fsm_step(&ctx, ev);
            XTime_GetTime(&t1);
            render_frame(&ctx);
            XTime_GetTime(&t2);
#ifdef SEP_DIAG
            diag_frame(&ctx, (u32)((t1 - t0) / (COUNTS_PER_SECOND / 1000000u)),
                       (u32)((t2 - t1) / (COUNTS_PER_SECOND / 1000000u)));
#else
            (void)t0; (void)t1; (void)t2;
#endif
        }
    }
    return 0;
}
