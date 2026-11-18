/*
 * event_queues.c - Colas de eventos entre las ISR y main: golpes (Hit_signal) y
 * eventos de interfaz (Btn_edges y Joy_dir hacia State_Machine).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §8.3). No es un bloque del diagrama: es el transporte
 * de las flechas que salen de ISR_handler y de Joystick_aconditioner.
 *
 * Colas circulares SPSC (un productor = ISR, un consumidor = main), de tamaño potencia
 * de 2 e índices volatile: no hace falta deshabilitar interrupciones para consumirlas.
 * La cola de UI tiene dos productores (timer1 y debouncer), pero las ISR no se anidan.
 */
#include "hw.h"

/* ------------------------------- colas ------------------------------------ */
typedef struct { Hit_signal buf[HIT_Q_LEN]; volatile u8 head, tail; } HitQ;      /* head: ISR, tail: main */
typedef struct { u8 buf[UI_Q_LEN];   volatile u8 head, tail; } UiQ;
static HitQ hq;
static UiQ  uq;

void hit_push(u8 lane, u32 song_ms)
{
    u8 nh = (u8)((hq.head + 1u) & (HIT_Q_LEN - 1u));
    if (nh != hq.tail) {                          /* llena: el golpe se descarta */
        hq.buf[hq.head].lane = lane;
        hq.buf[hq.head].t_ms = song_ms;
        hq.head = nh;
    }
}

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
