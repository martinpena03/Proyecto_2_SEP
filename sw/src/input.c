/*
 * input.c - Colas de eventos y botones filtrados por el IP propio (VERSION AVANCE).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §8.3). En el avance no hay joystick: los eventos de la
 * máquina de estados llegan de los botones de la Booster (S1, S2, JOY_SEL) a través de
 * debouncer_axi, o del teclado del PC (console.c).
 *
 * Colas circulares SPSC (un productor = ISR, un consumidor = main), de tamaño potencia
 * de 2 e índices volatile: no hace falta deshabilitar interrupciones para consumirlas.
 */
#include "hw.h"

/* ------------------------------- colas ------------------------------------ */
typedef struct { Hit buf[HIT_Q_LEN]; volatile u8 head, tail; } HitQ;      /* head: productor */
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

/* Sin joystick no hay modo juego que cambiar ni golpes que silenciar tras una sacudida;
 * se conservan por la interfaz de la máquina de estados (portable). */
void input_set_play_mode(int play)      { (void)play; }
void input_mute_hits_until(u32 sys_ms)  { (void)sys_ms; }

/* ------------------------------ botones (IP) ------------------------------ */
/* Interrupción del IP propio: lee los flancos ya filtrados y los borra (W1C).
 * S1 = OK, S2 = atrás / pausa, JOY_SEL = abajo (recorre los menús sin joystick). */
void deb_isr(void *ref)
{
    GameCtx *ctx = (GameCtx *)ref;
    u8 e = debouncer_get_edges(&ctx->deb);
    if (e & 0x1u) ui_push(UI_OK);        /* S1      */
    if (e & 0x2u) ui_push(UI_BACK);      /* S2      */
    if (e & 0x4u) ui_push(UI_DOWN);      /* JOY_SEL */
}
