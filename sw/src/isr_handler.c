/*
 * isr_handler.c - GIC, timers AXI y relojes del juego (ISR_handler, AO2).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: diseño propio (docs/MANUAL_IMPLEMENTACION.md §8.1) sobre los drivers XScuGic y
 *   XTmrCtr del BSP; el esquema de conexión al GIC sigue los ejemplos de Xilinx
 *   (xtmrctr_intr_example.c, xscugic_example.c). Las ayudantías no traen código de
 *   interrupciones: el enunciado del LAB10 solo pide "2 timers con interrupción".
 *
 * Bases de tiempo:
 *   axi_timer_0, 1 ms (GIC 61): sys_ms++, song_ms++ si la canción corre, star_ms-- y
 *                               buzzer_tick() (secuenciador de audio).
 *   axi_timer_1, 2 ms (GIC 62): joy_sample() (joystick -> golpes); cada 5 ticks
 *                               sensors_acc_sample() y sensors_mic_window(); cada 15
 *                               ticks levanta frame_flag (un frame cada 30 ms).
 *   Debouncer_IP (irq_deb, GIC 63): deb_isr() (Btn_edges: S1, S2, JOY_SEL).
 *   axi_gpio_1 (irq_gpio, GIC 64):  gpio_isr() (SENS_INT: DRDY del TMP006 ->
 *                               Temperature_aconditioner, AO2; INT del OPT3001 ->
 *                               Light_aconditioner si se encontró su pin).
 * Ninguna ISR dibuja ni usa el I2C: solo leen/escriben registros rápidos y dejan
 * banderas o colas para main.
 */
#include "xparameters.h"
#include "xscugic.h"
#include "xgpio.h"
#include "xtmrctr.h"
#include "xil_exception.h"
#include "xtime_l.h"
#include "hw.h"

/* IDs del GIC (IRQ_F2P[3:0] -> 61..64), con los nombres que genera el BSP. */
#define IRQ_TMR0   XPAR_FABRIC_AXI_TIMER_0_INTERRUPT_INTR
#define IRQ_TMR1   XPAR_FABRIC_AXI_TIMER_1_INTERRUPT_INTR
#define IRQ_DEB    XPAR_FABRIC_DEBOUNCER_IP_IRQ_INTR
#define IRQ_GPIO1  XPAR_FABRIC_AXI_GPIO_1_IP2INTC_IRPT_INTR

/* Cuenta descendente con auto-recarga: el periodo es TLR + 2 ciclos del reloj AXI. */
#define TMR_CLK_HZ      XPAR_AXI_TIMER_0_CLOCK_FREQ_HZ
#define TLR_FOR_MS(ms)  ((TMR_CLK_HZ / 1000u) * (ms) - 2u)

static XScuGic gic;
static XTmrCtr tmr0, tmr1;
static GameCtx *g_ctx;

static volatile u32 g_sys_ms, g_song_ms;
static volatile u8  g_song_run;
static u8 g_sens_div, g_frame_div;

volatile u8  frame_flag;
volatile u32 g_irq_cnt[4];
volatile u32 g_t1_max_us;

/* ------------------------------ relojes ---------------------------------- */
u32  clock_sys_ms(void)        { return g_sys_ms; }
u32  clock_song_ms(void)       { return g_song_ms; }
void clock_song_set(u32 ms)    { g_song_ms = ms; }
void clock_song_run(int run)   { g_song_run = (u8)(run != 0); }

/* 1 ms: reloj del sistema, reloj de la canción, star power y secuenciador. */
static void timer0_cb(void *ref, u8 n)
{
    GameCtx *ctx = (GameCtx *)ref;
    (void)n;
    g_irq_cnt[0]++;
    g_sys_ms++;
    if (g_song_run) {
        g_song_ms++;
        if (ctx_game_status(ctx)->star_ms) ctx_game_status(ctx)->star_ms--;
    }
    buzzer_tick(ctx_bz(ctx), g_song_ms);
}

/* 2 ms: joystick; cada 10 ms sensores; cada 30 ms un frame. */
static void timer1_cb(void *ref, u8 n)
{
    XTime t0, t1;
    u32 us;
    (void)ref;
    (void)n;
    XTime_GetTime(&t0);
    g_irq_cnt[1]++;
    joy_sample();
    if (++g_sens_div >= SENS_DIV)   { g_sens_div = 0;  sensors_acc_sample(); sensors_mic_window(); }
    if (++g_frame_div >= FRAME_DIV) { g_frame_div = 0; frame_flag = 1; }
    XTime_GetTime(&t1);
    us = (u32)((t1 - t0) / (COUNTS_PER_SECOND / 1000000u));
    if (us > g_t1_max_us) g_t1_max_us = us;       /* presupuesto: < 300 us */
}

/* ------------------------- irq_deb: botones (Debouncer_IP) ------------------ */
/* Interrupción del IP propio: lee los flancos ya filtrados y los borra (W1C). */
/* Pedidos de silencio pendientes (JOY_SEL); los atiende main con audio_user_mute_task(). */
volatile u8 g_mute_req;

void deb_isr(void *ref)
{
    GameCtx *ctx = (GameCtx *)ref;
    u8 btn_edges = debouncer_get_edges(&ctx->deb);        /* Btn_edges[2:0] */
    if (btn_edges & 0x1u) ui_push(UI_OK);        /* S1      */
    if (btn_edges & 0x2u) ui_push(UI_BACK);      /* S2      */
    /* JOY_SEL: una pulsación breve alterna la música (silencio con enclavamiento, no
     * "mientras esté presionado"). El EDGE del IP marca solo el flanco de presión. */
    if (btn_edges & 0x4u) g_mute_req++;
}

/* --------------- irq_gpio: SENS_INT (axi_gpio_1): DRDY del TMP006 e INT del OPT3001 ------- */
static XGpio gpio1;
volatile u8 opt_pending;
u8 g_opt_bit;
volatile u8  tmp_drdy;
volatile u32 g_tmp_irq;
u8 g_tmp_bit = SENS_BIT_NONE;

int gpio1_init(u8 opt_bit)
{
    g_opt_bit = opt_bit;
    if (XGpio_Initialize(&gpio1, XPAR_AXI_GPIO_1_DEVICE_ID) != XST_SUCCESS) return -1;
    XGpio_SetDataDirection(&gpio1, 1, 0xFFFFFFFFu);          /* todo entradas */
    XGpio_InterruptEnable(&gpio1, XGPIO_IR_CH1_MASK);
    XGpio_InterruptGlobalEnable(&gpio1);
    return 0;
}

u32 gpio1_read(void) { return XGpio_DiscreteRead(&gpio1, 1); }

/* La AXI GPIO interrumpe con cualquier cambio de la entrada. El DRDY del TMP006 y el INT
 * del OPT3001 son activos en bajo: solo interesa cuando el pin está en 0. El I2C lo hacen
 * temp_task() y theme_task() desde main. */
void gpio_isr(void *ref)
{
    u32 st = XGpio_InterruptGetStatus(&gpio1);
    u32 v;
    (void)ref;
    XGpio_InterruptClear(&gpio1, st);
    if ((st & XGPIO_IR_CH1_MASK) == 0u) return;
    v = XGpio_DiscreteRead(&gpio1, 1);
    if (g_tmp_bit != SENS_BIT_NONE && ((v >> g_tmp_bit) & 1u) == 0u) {
        tmp_drdy = 1;                            /* AO2: el TMP006 terminó una conversión */
        g_tmp_irq++;
    }
    if (g_opt_bit != OPT_BIT_NONE && ((v >> g_opt_bit) & 1u) == 0u) {
        opt_pending = 1;                            /* sin pin INT: respaldo por timer */
    }
}

static void deb_isr_wrap(void *ref)  { g_irq_cnt[2]++; deb_isr(ref); }
static void gpio_isr_wrap(void *ref) { g_irq_cnt[3]++; gpio_isr(ref); }

/* Configura el GIC y los dos timers. Devuelve 0 si todo salió bien. */
int isr_init(GameCtx *ctx)
{
    XScuGic_Config *cfg;
    XTmrCtr *t[2] = { &tmr0, &tmr1 };
    u16 id[2]  = { XPAR_AXI_TIMER_0_DEVICE_ID, XPAR_AXI_TIMER_1_DEVICE_ID };
    u32 tlr[2] = { TLR_FOR_MS(1u), TLR_FOR_MS(JOY_TICK_MS) };
    XTmrCtr_Handler cb[2] = { timer0_cb, timer1_cb };
    int i;

    g_ctx = ctx;
    cfg = XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
    if (cfg == NULL || XScuGic_CfgInitialize(&gic, cfg, cfg->CpuBaseAddress) != XST_SUCCESS) return -1;
    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                                 (Xil_ExceptionHandler)XScuGic_InterruptHandler, &gic);

    /* Timers: interrupción + auto-recarga + cuenta descendente. */
    for (i = 0; i < 2; i++) {
        if (XTmrCtr_Initialize(t[i], id[i]) != XST_SUCCESS) return -2;
        XTmrCtr_SetHandler(t[i], cb[i], ctx);
        XTmrCtr_SetOptions(t[i], 0, XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION | XTC_DOWN_COUNT_OPTION);
        XTmrCtr_SetResetValue(t[i], 0, tlr[i]);
    }

    /* Prioridad (menor = más prioritaria) y disparo 0x1 = nivel alto. */
    XScuGic_SetPriorityTriggerType(&gic, IRQ_TMR0,  0xA0, 0x1);
    XScuGic_SetPriorityTriggerType(&gic, IRQ_TMR1,  0xA8, 0x1);
    XScuGic_SetPriorityTriggerType(&gic, IRQ_DEB,   0xB0, 0x1);
    XScuGic_SetPriorityTriggerType(&gic, IRQ_GPIO1, 0xB8, 0x1);
    if (XScuGic_Connect(&gic, IRQ_TMR0,  (Xil_InterruptHandler)XTmrCtr_InterruptHandler, &tmr0) != XST_SUCCESS) return -3;
    if (XScuGic_Connect(&gic, IRQ_TMR1,  (Xil_InterruptHandler)XTmrCtr_InterruptHandler, &tmr1) != XST_SUCCESS) return -3;
    if (XScuGic_Connect(&gic, IRQ_DEB,   (Xil_InterruptHandler)deb_isr_wrap,  ctx) != XST_SUCCESS) return -3;
    if (XScuGic_Connect(&gic, IRQ_GPIO1, (Xil_InterruptHandler)gpio_isr_wrap, ctx) != XST_SUCCESS) return -3;
    XScuGic_Enable(&gic, IRQ_TMR0);
    XScuGic_Enable(&gic, IRQ_TMR1);
    XScuGic_Enable(&gic, IRQ_DEB);
    XScuGic_Enable(&gic, IRQ_GPIO1);

    Xil_ExceptionEnable();
    XTmrCtr_Start(&tmr0, 0);
    XTmrCtr_Start(&tmr1, 0);
    return 0;
}
