/*
 * input.c - Joystick (golpes y menús), botones filtrados por el IP y OPT_INT.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §8.3); el ADC se lee con el driver del curso (lib/ADC.c).
 * Cubre AC3 (joystick y botones de la Booster) y parte de AO2 (interrupciones).
 *
 * Colas circulares SPSC (un productor = ISR, un consumidor = main), de tamaño potencia
 * de 2 e índices volatile: no hace falta deshabilitar interrupciones para consumirlas.
 * La cola de UI tiene dos productores (timer1 y debouncer), pero las ISR no se anidan.
 */
#include "xparameters.h"
#include "xgpio.h"
#include "sleep.h"
#include "hw.h"
#include "lib/ADC.h"

/* ------------------------------- colas ------------------------------------ */
typedef struct { Hit buf[HIT_Q_LEN]; volatile u8 head, tail; } HitQ;      /* head: ISR, tail: main */
typedef struct { u8 buf[UI_Q_LEN];   volatile u8 head, tail; } UiQ;
static HitQ hq;
static UiQ  uq;

void hit_push(u8 lane, u32 song_ms)
{
    u8 nh = (u8)((hq.head + 1u) & (HIT_Q_LEN - 1u));
    if (nh != hq.tail) {                          /* llena: el golpe se descarta */
        hq.buf[hq.head].lane = lane;
        hq.buf[hq.head].song_ms = song_ms;
        hq.head = nh;
    }
}

int hit_pop(Hit *out)
{
    if (hq.tail == hq.head) return 0;
    *out = hq.buf[hq.tail];
    hq.tail = (u8)((hq.tail + 1u) & (HIT_Q_LEN - 1u));
    return 1;
}

void ui_push(UiEvent ev)
{
    u8 nh = (u8)((uq.head + 1u) & (UI_Q_LEN - 1u));
    if (nh != uq.tail) {
        uq.buf[uq.head] = (u8)ev;
        uq.head = nh;
    }
}

UiEvent ui_pop(void)
{
    UiEvent ev;
    if (uq.tail == uq.head) return UI_NONE;
    ev = (UiEvent)uq.buf[uq.tail];
    uq.tail = (u8)((uq.tail + 1u) & (UI_Q_LEN - 1u));
    return ev;
}

void input_flush(void)
{
    hq.tail = hq.head;
    uq.tail = uq.head;
}

/* ------------------------------ joystick ---------------------------------- */
static JoyDet g_joy;
static volatile u8  g_play_mode;
static volatile u32 g_mute_until;
static volatile u16 g_last_x, g_last_y;

void input_set_play_mode(int play)      { g_play_mode = (u8)(play != 0); }
void input_set_joy_umbral(u16 umbral)   { g_joy.umbral = umbral; }
void input_set_joy_inv_y(u8 inv)        { g_joy.inv_y = inv; }
void input_mute_hits_until(u32 sys_ms)  { g_mute_until = sys_ms; }

void input_get_joy(u16 *x, u16 *y, u16 *x0, u16 *y0)
{
    *x = g_last_x;  *y = g_last_y;
    *x0 = (u16)g_joy.x0;  *y0 = (u16)g_joy.y0;
}

/* Calibra el centro del joystick (promedio de 32 lecturas con la palanca suelta).
 * Se llama antes de habilitar las interrupciones. */
int input_init(void)
{
    static const u8 ch[2] = { ADC_CH_JOYX, ADC_CH_JOYY };
    u32 sx = 0, sy = 0;
    u16 v[2];
    int i;
    for (i = 0; i < 32; i++) {
        adc_read_chain(ch, 2, v);
        sx += v[0];
        sy += v[1];
        usleep(1000);
    }
    joy_det_init(&g_joy, (u16)(sx / 32u), (u16)(sy / 32u), JOY_UMBRAL_DEF, JOY_INVERT_Y);
    g_last_x = (u16)g_joy.x0;
    g_last_y = (u16)g_joy.y0;
    /* Un centro muy corrido indica que la palanca estaba movida o que el ADC no responde. */
    if (g_joy.x0 < 300 || g_joy.x0 > 724 || g_joy.y0 < 300 || g_joy.y0 > 724) return -1;
    return 0;
}

/* Desde timer1 (cada 2 ms): lee X, Y y el micrófono en una ráfaga y detecta flechas. */
void joy_sample(void)
{
    static const u8 ch[3] = { ADC_CH_JOYX, ADC_CH_JOYY, ADC_CH_MIC };
    u16 v[3];
    int d;

    adc_read_chain(ch, 3, v);
    g_last_x = v[0];
    g_last_y = v[1];
    sensors_feed_mic(v[2]);

    d = joy_detect(&g_joy, v[0], v[1]);
    if (d < 0) return;
    if (g_play_mode) {                               /* JUEGO: golpe con sello de tiempo */
        if (clock_sys_ms() < g_mute_until) return;   /* recién hubo una sacudida */
        /* El detector decide JOY_SETTLE_N muestras después de cruzar el umbral: el
         * sello de tiempo se corrige para que marque el instante del cruce. */
        {
            u32 t = clock_song_ms(), lat = JOY_SETTLE_N * JOY_TICK_MS;
            hit_push((u8)d, (t > lat) ? (t - lat) : 0u);
        }
    } else if (d == ARRIBA) {                        /* menús: solo el eje vertical */
        ui_push(UI_UP);
    } else if (d == ABAJO) {
        ui_push(UI_DOWN);
    }
}

/* ------------------------------ botones (IP) ------------------------------ */
/* Interrupción del IP propio: lee los flancos ya filtrados y los borra (W1C). */
void deb_isr(void *ref)
{
    GameCtx *ctx = (GameCtx *)ref;
    u8 e = debouncer_get_edges(&ctx->deb);
    if (e & 0x1u) ui_push(UI_OK);        /* S1      */
    if (e & 0x2u) ui_push(UI_BACK);      /* S2      */
    if (e & 0x4u) ui_push(UI_OK);        /* JOY_SEL */
}

/* --------------------------- OPT_INT (axi_gpio_1) ------------------------- */
static XGpio gpio1;
volatile u8 opt_pending;
u8 g_opt_bit;

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

/* La AXI GPIO interrumpe con cualquier cambio de la entrada. El INT del OPT3001 es
 * activo en bajo: solo interesa cuando el pin está en 0. El I2C lo hace theme_task(). */
void gpio_isr(void *ref)
{
    u32 st = XGpio_InterruptGetStatus(&gpio1);
    (void)ref;
    XGpio_InterruptClear(&gpio1, st);
    if (g_opt_bit == OPT_BIT_NONE) return;          /* sin pin INT: respaldo por timer */
    if ((st & XGPIO_IR_CH1_MASK) && ((XGpio_DiscreteRead(&gpio1, 1) >> g_opt_bit) & 1u) == 0u) {
        opt_pending = 1;
    }
}
