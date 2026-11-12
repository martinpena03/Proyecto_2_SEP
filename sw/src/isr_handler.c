/*
 * isr_handler.c - GIC, timers AXI y relojes del juego (ISR_handler, AO2), VERSION AVANCE.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: diseño propio (docs/MANUAL_IMPLEMENTACION.md §8.1) sobre los drivers XScuGic y
 *   XTmrCtr del BSP; el esquema de conexión al GIC sigue los ejemplos de Xilinx
 *   (xtmrctr_intr_example.c, xscugic_example.c).
 *
 * Interrupciones (IRQ_F2P[2:0] -> GIC 61..63):
 *   axi_timer_0, 1 ms (GIC 61): sys_ms++, song_ms++ si la canción corre, star_ms-- y
 *                               buzzer_tick() (secuenciador de la melodía).
 *   axi_timer_1, 2 ms (GIC 62): cada 5 ticks sensors_acc_sample() (acelerómetro -> evento
 *                               "movimiento"); cada 100 ticks levanta g_opt_poll (revisar
 *                               el comparador del OPT3001 -> "Mucha luz"); cada 15 ticks
 *                               levanta g_frame_flag (un paso de la FSM cada 30 ms).
 *   Debouncer_IP (irq_deb, GIC 63): deb_isr() (S1 = OK, S2 = atrás, JOY_SEL = música ON/OFF).
 * Ninguna ISR usa el I2C ni la UART: solo leen/escriben registros rápidos y dejan
 * banderas o colas para main.
 */
#include "xparameters.h"
#include "xscugic.h"
#include "xtmrctr.h"
#include "xil_exception.h"
#include "xtime_l.h"
#include "hw.h"

#define IRQ_TMR0   XPAR_FABRIC_AXI_TIMER_0_INTERRUPT_INTR
#define IRQ_TMR1   XPAR_FABRIC_AXI_TIMER_1_INTERRUPT_INTR
#define IRQ_DEB    XPAR_FABRIC_DEBOUNCER_IP_IRQ_INTR

/* Cuenta descendente con auto-recarga: el periodo es TLR + 2 ciclos del reloj AXI. */
#define TMR_CLK_HZ      XPAR_AXI_TIMER_0_CLOCK_FREQ_HZ
#define TLR_FOR_MS(ms)  ((TMR_CLK_HZ / 1000u) * (ms) - 2u)

static XScuGic gic;
static XTmrCtr tmr0, tmr1;

static volatile u32 g_sys_ms, g_song_ms;
static volatile u8  g_song_run;
static u8  g_sens_div, g_frame_div, g_opt_div;

volatile u8  g_frame_flag;
volatile u8  g_opt_poll;
volatile u32 g_irq_cnt[3];
volatile u32 g_t1_max_us;

/* ------------------------------ relojes ---------------------------------- */
u32  clock_sys_ms(void)        { return g_sys_ms; }
u32  clock_song_ms(void)       { return g_song_ms; }
void clock_song_set(u32 ms)    { g_song_ms = ms; }
void clock_song_run(int run)   { g_song_run = (u8)(run != 0); }

/* 1 ms: reloj del sistema, reloj de la canción, star power y secuenciador del buzzer. */
static void timer0_cb(void *ref, u8 n)
{
    GameCtx *ctx = (GameCtx *)ref;
    (void)n;
    g_irq_cnt[0]++;
    g_sys_ms++;
    if (g_song_run) {
        g_song_ms++;
        if (ctx_gs(ctx)->star_ms) ctx_gs(ctx)->star_ms--;
    }
    buzzer_tick(ctx_bz(ctx), g_song_ms);
}

/* 2 ms: cada 10 ms acelerómetro, cada 30 ms un frame, cada 200 ms el sensor de luz. */
static void timer1_cb(void *ref, u8 n)
{
    XTime t0, t1;
    u32 us;
    (void)ref;
    (void)n;
    XTime_GetTime(&t0);
    g_irq_cnt[1]++;
    if (++g_sens_div >= SENS_DIV)     { g_sens_div = 0;  sensors_acc_sample(); }
    if (++g_frame_div >= FRAME_DIV)   { g_frame_div = 0; g_frame_flag = 1; }
    if (++g_opt_div >= OPT_POLL_DIV)  { g_opt_div = 0;   g_opt_poll = 1; }
    XTime_GetTime(&t1);
    us = (u32)((t1 - t0) / (COUNTS_PER_SECOND / 1000000u));
    if (us > g_t1_max_us) g_t1_max_us = us;       /* presupuesto: < 300 us */
}

/* ------------------------- irq_deb: botones (Debouncer_IP) ------------------ */
/* Interrupción del IP propio: lee los flancos ya filtrados y los borra (W1C).
 * S1 = OK, S2 = atrás / pausa, JOY_SEL = música ON / OFF (silencio enclavado: cada
 * pulsación lo alterna). La ISR solo cuenta; main imprime el aviso (btn_task) y cambia
 * el mudo (audio_user_mute_task), porque la UART no se usa dentro de una interrupción. */
volatile u8 g_mute_req;                  /* pulsaciones de JOY_SEL pendientes           */
volatile u8 g_btn_cnt[3];                /* pulsaciones vistas por la ISR: S1, S2, JOY_SEL */

void deb_isr(void *ref)
{
    GameCtx *ctx = (GameCtx *)ref;
    u8 e = debouncer_get_edges(&ctx->deb);
    if (e & 0x1u) { g_btn_cnt[0]++; ui_push(UI_OK); }      /* S1      */
    if (e & 0x2u) { g_btn_cnt[1]++; ui_push(UI_BACK); }    /* S2      */
    if (e & 0x4u) { g_btn_cnt[2]++; g_mute_req++; }        /* JOY_SEL */
}

static void deb_isr_wrap(void *ref) { g_irq_cnt[2]++; deb_isr(ref); }

/* Configura el GIC y los dos timers. Devuelve 0 si todo salió bien. */
int isr_init(GameCtx *ctx)
{
    XScuGic_Config *cfg;
    XTmrCtr *t[2] = { &tmr0, &tmr1 };
    u16 id[2]  = { XPAR_AXI_TIMER_0_DEVICE_ID, XPAR_AXI_TIMER_1_DEVICE_ID };
    u32 tlr[2] = { TLR_FOR_MS(1u), TLR_FOR_MS(TICK1_MS) };
    XTmrCtr_Handler cb[2] = { timer0_cb, timer1_cb };
    int i;

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
    XScuGic_SetPriorityTriggerType(&gic, IRQ_TMR0, 0xA0, 0x1);
    XScuGic_SetPriorityTriggerType(&gic, IRQ_TMR1, 0xA8, 0x1);
    XScuGic_SetPriorityTriggerType(&gic, IRQ_DEB,  0xB0, 0x1);
    if (XScuGic_Connect(&gic, IRQ_TMR0, (Xil_InterruptHandler)XTmrCtr_InterruptHandler, &tmr0) != XST_SUCCESS) return -3;
    if (XScuGic_Connect(&gic, IRQ_TMR1, (Xil_InterruptHandler)XTmrCtr_InterruptHandler, &tmr1) != XST_SUCCESS) return -3;
    if (XScuGic_Connect(&gic, IRQ_DEB,  (Xil_InterruptHandler)deb_isr_wrap, ctx) != XST_SUCCESS) return -3;
    XScuGic_Enable(&gic, IRQ_TMR0);
    XScuGic_Enable(&gic, IRQ_TMR1);
    XScuGic_Enable(&gic, IRQ_DEB);

    Xil_ExceptionEnable();
    XTmrCtr_Start(&tmr0, 0);
    XTmrCtr_Start(&tmr1, 0);
    return 0;
}
