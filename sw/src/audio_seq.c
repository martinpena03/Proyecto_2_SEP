/*
 * audio_seq.c - Secuenciador no bloqueante del buzzer (Sound Manager, AC6).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: idea del reproductor no bloqueante de la cápsula "Buzzer Proyecto 2" del
 *   curso (Buzzer_Tick llamado desde la interrupción de un timer, sin usleep);
 *   implementación propia (docs/MANUAL_IMPLEMENTACION.md §8.6).
 *
 * Portable: solo llama a buzzer_tone() (audio.c en la placa: PWM del TTC0; stub en el PC).
 *
 * buzzer_tick() se llama cada 1 ms desde la interrupción de axi_timer_0. Hay dos pistas:
 *   - principal: la canción (sigue el reloj de canción song_ms, así la pausa la congela)
 *     o un jingle (reloj propio, opcionalmente en loop);
 *   - efecto (sfx): un sonido corto que tapa la pista principal mientras dura.
 * Las funciones buzzer_play* se llaman desde main y la interrupción puede caer en medio:
 * por eso primero se baja la bandera 'playing' / 'sfx_on', se cambian los campos y al
 * final se vuelve a subir (la ISR nunca ve una pista a medio configurar).
 */
#include "interfaces.h"

/* Frecuencias de las notas usadas en los jingles [Hz] (octavas 5 a 7: el buzzer
 * pasivo de la BoosterPack suena mejor entre 1 y 4 kHz). */
#define N_C5   523u
#define N_E5   659u
#define N_F5   698u
#define N_G5   784u
#define N_C6  1047u
#define N_D6  1175u
#define N_E6  1319u
#define N_FS6 1480u
#define N_G6  1568u
#define N_A6  1760u
#define N_B6  1976u
#define N_C7  2093u
#define N_E7  2637u
#define N_G7  3136u

/* Jingle del menú: arpegio de 4 compases a 120 BPM (corchea = 250 ms), en loop. */
const Note JINGLE_MENU[] = {
    {    0, IZQ, N_E6,  220 }, {  250, IZQ, N_G6,  220 }, {  500, IZQ, N_B6,  220 }, {  750, IZQ, N_G6,  220 },
    { 1000, IZQ, N_E6,  220 }, { 1250, IZQ, N_G6,  220 }, { 1500, IZQ, N_C7,  220 }, { 1750, IZQ, N_G6,  220 },
    { 2000, IZQ, N_D6,  220 }, { 2250, IZQ, N_FS6, 220 }, { 2500, IZQ, N_A6,  220 }, { 2750, IZQ, N_FS6, 220 },
    { 3000, IZQ, N_D6,  220 }, { 3250, IZQ, N_G6,  220 }, { 3500, IZQ, N_B6,  220 }, { 3750, IZQ, N_G6,  220 },
    { 3970, IZQ, 0,      30 },                              /* silencio: cierra el compás */
};
const u16 JINGLE_MENU_N = (u16)(sizeof(JINGLE_MENU) / sizeof(JINGLE_MENU[0]));

/* Victoria: arpegio ascendente. */
const Note JINGLE_WIN[] = {
    {   0, IZQ, N_C6, 140 }, { 150, IZQ, N_E6, 140 }, { 300, IZQ, N_G6, 140 },
    { 450, IZQ, N_C7, 140 }, { 600, IZQ, N_G6, 140 }, { 750, IZQ, N_C7, 450 },
};
const u16 JINGLE_WIN_N = (u16)(sizeof(JINGLE_WIN) / sizeof(JINGLE_WIN[0]));

/* Derrota: descenso. */
const Note JINGLE_LOSE[] = {
    {   0, IZQ, N_G5, 240 }, { 250, IZQ, N_F5, 240 }, { 500, IZQ, N_E5, 240 }, { 750, IZQ, N_C5, 600 },
};
const u16 JINGLE_LOSE_N = (u16)(sizeof(JINGLE_LOSE) / sizeof(JINGLE_LOSE[0]));

/* Efectos cortos. */
const Note SFX_COUNT[]  = { { 0, IZQ, 1000, 120 } };                 /* cuenta 3-2-1 */
const u16  SFX_COUNT_N  = 1;
const Note SFX_GO[]     = { { 0, IZQ, 2000, 250 } };
const u16  SFX_GO_N     = 1;
const Note SFX_STAR[]   = {                                          /* star power */
    { 0, IZQ, N_C6, 55 }, { 60, IZQ, N_E6, 55 }, { 120, IZQ, N_G6, 55 },
    { 180, IZQ, N_C7, 55 }, { 240, IZQ, N_E7, 80 },
};
const u16  SFX_STAR_N   = (u16)(sizeof(SFX_STAR) / sizeof(SFX_STAR[0]));
const Note SFX_ENCORE[] = {                                          /* bonus encore */
    { 0, IZQ, N_G6, 75 }, { 80, IZQ, N_C7, 75 }, { 160, IZQ, N_E7, 75 }, { 240, IZQ, N_G7, 160 },
};
const u16  SFX_ENCORE_N = (u16)(sizeof(SFX_ENCORE) / sizeof(SFX_ENCORE[0]));

/* Deja el secuenciador detenido y fija el volumen (0..100 %). */
void buzzer_init(Buzzer_t *bz, u8 vol)
{
    bz->playing = 0;
    bz->sfx_on  = 0;
    bz->paused  = 0;
    bz->notes   = NULL;
    bz->sfx     = NULL;
    bz->n = bz->idx = 0;
    bz->sfx_n = bz->sfx_idx = 0;
    bz->loop = 0;
    bz->song_clock = 0;
    bz->t_ms = bz->sfx_t = 0;
    bz->cur_f = 0;
    bz->vol = vol;
}

/* Canción: la nota i suena en [t_ms, t_ms + dur) del reloj de canción. */
void buzzer_play(Buzzer_t *bz, const Song *song)
{
    bz->playing    = 0;
    bz->notes      = song->chart;
    bz->n          = song->n;
    bz->idx        = 0;
    bz->loop       = 0;
    bz->song_clock = 1;
    bz->paused     = 0;
    bz->playing    = 1;
}

/* Jingle: los t_ms de las notas son relativos al inicio; usa un reloj propio. */
void buzzer_play_jingle(Buzzer_t *bz, const Note *notes, u16 n, u8 loop)
{
    bz->playing    = 0;
    bz->notes      = notes;
    bz->n          = n;
    bz->idx        = 0;
    bz->loop       = loop;
    bz->song_clock = 0;
    bz->t_ms       = 0;
    bz->paused     = 0;
    bz->playing    = (u8)(n > 0u);
}

/* Efecto corto superpuesto: mientras dura, tapa la pista principal. */
void buzzer_play_sfx(Buzzer_t *bz, const Note *notes, u16 n)
{
    bz->sfx_on  = 0;
    bz->sfx     = notes;
    bz->sfx_n   = n;
    bz->sfx_idx = 0;
    bz->sfx_t   = 0;
    bz->sfx_on  = (u8)(n > 0u);
}

/* Pausa: silencia la pista principal sin perder la posición (la canción se congela
 * porque song_ms deja de avanzar). */
void buzzer_pause(Buzzer_t *bz, int on)
{
    bz->paused = (u8)(on != 0);
}

void buzzer_stop(Buzzer_t *bz)
{
    bz->playing = 0;
    bz->sfx_on  = 0;
    bz->paused  = 0;
    buzzer_tone(bz, 0, 0);
}

/* Avanza 1 ms. Decide la frecuencia que debe sonar ahora y llama a buzzer_tone()
 * SOLO si cambió respecto de la que ya suena (cur_f). */
void buzzer_tick(Buzzer_t *bz, u32 song_ms)
{
    u16 want = 0;

    if (bz->playing) {
        u32 now = bz->song_clock ? song_ms : bz->t_ms++;
        /* Salta las notas que ya terminaron. */
        while (bz->idx < bz->n && now >= bz->notes[bz->idx].t_ms + bz->notes[bz->idx].dur) {
            bz->idx++;
        }
        if (bz->idx >= bz->n) {
            if (bz->loop) {                    /* jingle en loop: vuelve a empezar */
                bz->idx  = 0;
                bz->t_ms = 0;
            } else if (!bz->song_clock) {
                bz->playing = 0;               /* jingle terminado */
            }
        } else if (bz->notes[bz->idx].t_ms <= now && !bz->paused) {
            want = bz->notes[bz->idx].freq;    /* dentro de la nota actual */
        }
    }

    if (bz->sfx_on) {
        u32 t = bz->sfx_t++;
        while (bz->sfx_idx < bz->sfx_n &&
               t >= bz->sfx[bz->sfx_idx].t_ms + bz->sfx[bz->sfx_idx].dur) {
            bz->sfx_idx++;
        }
        if (bz->sfx_idx >= bz->sfx_n) {
            bz->sfx_on = 0;                    /* efecto terminado: vuelve la pista */
        } else {
            want = (bz->sfx[bz->sfx_idx].t_ms <= t) ? bz->sfx[bz->sfx_idx].freq : 0u;
        }
    }

    if (want != bz->cur_f) buzzer_tone(bz, want, bz->vol);
}
