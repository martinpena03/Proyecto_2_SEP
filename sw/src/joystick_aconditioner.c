/*
 * joystick_aconditioner.c - Joystick_aconditioner, VERSION AVANCE: lee X, Y del joystick
 * por el ADC y entrega Hit_signal (lane, t_ms) a Game Manager durante el juego.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: joystick_aconditioner.c de la
 * rama proyecto-final (docs/MANUAL_IMPLEMENTACION.md §8.3); el ADC se lee con el driver
 * del curso (lib/ADC.c). El detector de dirección portable está en
 * joystick_aconditioner_logic.c; las colas, en event_queues.c.
 *
 * Diferencias con la versión final: en el avance los menús se siguen manejando por la
 * consola (u/j), así que el joystick solo genera golpes en PLAY, y no se lee el micrófono.
 */
#include "sleep.h"
#include "hw.h"
#include "lib/ADC.h"

static JoyDet g_joy;
static volatile u8  g_play_mode;
static volatile u32 g_mute_until;

void input_set_play_mode(int play)      { g_play_mode = (u8)(play != 0); }
void input_mute_hits_until(u32 sys_ms)  { g_mute_until = sys_ms; }

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
    /* Un centro muy corrido indica que la palanca estaba movida o que el ADC no responde. */
    if (g_joy.x0 < 300 || g_joy.x0 > 724 || g_joy.y0 < 300 || g_joy.y0 > 724) return -1;
    return 0;
}

/* Desde timer1 (cada 2 ms): lee X, Y en una ráfaga y, en PLAY, encola la flecha. */
void joy_sample(void)
{
    static const u8 ch[2] = { ADC_CH_JOYX, ADC_CH_JOYY };
    u16 v[2];
    int d;

    adc_read_chain(ch, 2, v);
    d = joy_detect(&g_joy, v[0], v[1]);
    if (d < 0) return;

    if (g_play_mode) {                               /* JUEGO: golpe con sello de tiempo */
        if (clock_sys_ms() < g_mute_until) return;   /* recién hubo una sacudida */
        u32 t = clock_song_ms(), lat = JOY_SETTLE_N * JOY_TICK_MS;
        hit_push_joy((u8)d, (t > lat) ? (t - lat) : 0u);
    } else if (d == ARRIBA) {                        /* MENÚS: mover el cursor */
        ui_push(UI_UP);
    } else if (d == ABAJO) {
        ui_push(UI_DOWN);
    }
}
