/*
 * event_queues.c - Colas de eventos entre las ISR y main (VERSION AVANCE).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §8.3). No es un bloque del diagrama: es el transporte
 * de Btn_edges y Hit_signal hacia State_Machine. Los eventos de interfaz llegan de los
 * botones de la Booster (S1, S2, JOY_SEL) a través de Debouncer_IP (la ISR está en
 * isr_handler.c) o del teclado del PC (uart_console.c). Los golpes llegan del joystick
 * (joystick_aconditioner.c) o del autoplay de la consola.
 *
 * Colas circulares SPSC (un productor = ISR, un consumidor = main), de tamaño potencia
 * de 2 e índices volatile: no hace falta deshabilitar interrupciones para consumirlas.
 */
#include "hw.h"

/* ------------------------------- colas ------------------------------------ */
typedef struct { Hit_signal buf[HIT_Q_LEN]; volatile u8 head, tail; } HitQ;      /* head: productor */
typedef struct { u8 buf[UI_Q_LEN];   volatile u8 head, tail; } UiQ;
static HitQ hq;
static UiQ  uq;

static void hit_push_src(u8 lane, u32 song_ms, u8 from_joy)
{
    u8 nh = (u8)((hq.head + 1u) & (HIT_Q_LEN - 1u));
    if (nh != hq.tail) {                          /* llena: el golpe se descarta */
        hq.buf[hq.head].lane = lane;
        hq.buf[hq.head].from_joy = from_joy;
        hq.buf[hq.head].t_ms = song_ms;
        hq.head = nh;
    }
}

void hit_push(u8 lane, u32 song_ms)     { hit_push_src(lane, song_ms, 0u); }   /* autoplay */
void hit_push_joy(u8 lane, u32 song_ms) { hit_push_src(lane, song_ms, 1u); }   /* joystick */

int hit_pop(Hit_signal *out)
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
