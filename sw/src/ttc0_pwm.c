/*
 * ttc0_pwm.c - PWM del buzzer con el TTC0 del PS, ruteado por EMIO (AC6).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: técnica de la cápsula "Buzzer Proyecto 2" del curso (opción A: TTC0 en modo
 *   interval + match, TTC0_WAVE0_OUT -> EMIO -> pin T14 -> buzzer; silencio con
 *   match > intervalo) sobre el driver XTtcPs del BSP; implementación propia
 *   (docs/MANUAL_IMPLEMENTACION.md §8.6). No usa VHDL: el PWM lo genera el PS.
 *
 * buzzer_tone() lo llama el secuenciador (sound_manager.c) desde la interrupción de 1 ms,
 * solo cuando cambia la nota.
 *
 * MUDO (SEP_MUTE o audio_set_mute(1)): el contador del TTC queda detenido y con la
 * salida de onda deshabilitada (el estado de reset), así el pin no conmuta. Los
 * registros de intervalo, prescaler y match se siguen escribiendo, para poder verificar
 * por lectura que la frecuencia programada es la correcta sin hacer sonar la placa.
 */
#include "xparameters.h"
#include "xttcps.h"
#include "xil_printf.h"
#include "hw.h"

static XTtcPs ttc;                 /* TTC0, contador 0 -> TTC0_WAVE0_OUT */
static volatile u8 g_mute = SEP_MUTE;
static u8 g_ready;

/* Opciones con sonido: intervalo + match + polaridad (onda habilitada). */
#define OPT_SOUND  (XTTCPS_OPTION_INTERVAL_MODE | XTTCPS_OPTION_MATCH_MODE | XTTCPS_OPTION_WAVE_POLARITY)
/* Opciones en mudo: igual, pero con la salida de onda deshabilitada. */
#define OPT_MUTED  (XTTCPS_OPTION_INTERVAL_MODE | XTTCPS_OPTION_MATCH_MODE | XTTCPS_OPTION_WAVE_DISABLE)

/* Deja el TTC inicializado y el buzzer en silencio. Es lo PRIMERO que hace main. */
int audio_hw_init(void)
{
    XTtcPs_Config *c = XTtcPs_LookupConfig(XPAR_XTTCPS_0_DEVICE_ID);
    int s;
    if (c == NULL) return -1;
    s = XTtcPs_CfgInitialize(&ttc, c, c->BaseAddress);
    if (s == XST_DEVICE_IS_STARTED) {           /* quedó corriendo de una carga anterior */
        XTtcPs_Stop(&ttc);
        s = XTtcPs_CfgInitialize(&ttc, c, c->BaseAddress);
    }
    if (s != XST_SUCCESS) return -2;
    XTtcPs_Stop(&ttc);
    (void)XTtcPs_SetOptions(&ttc, g_mute ? OPT_MUTED : OPT_SOUND);
    XTtcPs_SetMatchValue(&ttc, 0, 0xFFFFu);     /* match > intervalo: sin conmutaciones */
    XTtcPs_SetInterval(&ttc, 0xFFFEu);
    g_ready = 1;
    return 0;
}

int audio_is_muted(void) { return g_mute; }

/* Silencio pedido por el usuario con JOY_SEL (enclavado: cada pulsación lo alterna).
 * Es independiente del mudo de seguridad: con SEP_MUTE = 1 el botón no puede habilitar
 * el sonido. El secuenciador sigue avanzando en silencio, así al volver la música
 * retoma en la nota que corresponde. Se llama desde main, no desde la ISR. */
static u8 g_user_mute;

int audio_user_muted(void) { return g_user_mute; }

void audio_user_mute_task(void)
{
    static u8 seen;
    u8 req = g_mute_req;
    if (req == seen) return;
    if ((u8)(req - seen) & 1u) g_user_mute = (u8)!g_user_mute;   /* pulsaciones impares */
    seen = req;
    if (!SEP_MUTE) audio_set_mute(g_user_mute);
    xil_printf("[BUZ] JOY_SEL -> musica %s%s\r\n", g_user_mute ? "OFF (silencio)" : "ON",
               SEP_MUTE ? " (build en mudo: el buzzer sigue deshabilitado)" : "");
}

/* Habilita o deshabilita la salida. Al cambiar deja el buzzer en silencio; la próxima
 * nota del secuenciador lo vuelve a programar. */
void audio_set_mute(int mute)
{
    g_mute = (u8)(mute != 0);
    if (!g_ready) return;
    XTtcPs_Stop(&ttc);
    (void)XTtcPs_SetOptions(&ttc, g_mute ? OPT_MUTED : OPT_SOUND);
    XTtcPs_SetMatchValue(&ttc, 0, 0xFFFFu);
    XTtcPs_SetInterval(&ttc, 0xFFFEu);
    if (!g_mute) XTtcPs_Start(&ttc);
}

/* Programa una nota: f_hz = frecuencia (0 = silencio), vol = 0..100 (duty 0..50 %). */
void buzzer_tone(Buzzer_t *bz, u16 f_hz, u8 vol)
{
    XInterval interval;
    u8 presc;

    if (bz != NULL) bz->cur_f = f_hz;
    if (!g_ready) return;
    if (f_hz == 0u || vol == 0u) {              /* silencio */
        XTtcPs_SetMatchValue(&ttc, 0, 0xFFFFu);
        XTtcPs_SetInterval(&ttc, 0xFFFEu);
        return;
    }
    XTtcPs_Stop(&ttc);
    XTtcPs_CalcIntervalFromFreq(&ttc, f_hz, &interval, &presc);
    XTtcPs_SetInterval(&ttc, interval);
    XTtcPs_SetPrescaler(&ttc, presc);
    /* El volumen se regula con el duty: el máximo de la fundamental está en 50 %. */
    XTtcPs_SetMatchValue(&ttc, 0, (XMatchRegValue)((u32)interval * vol / 200u));
    XTtcPs_ResetCounterValue(&ttc);
    if (!g_mute) XTtcPs_Start(&ttc);            /* en mudo el contador queda detenido */
}

/* Registros actuales del TTC0 (verificación sin sonido y diagnóstico). */
void audio_get_regs(u32 *interval, u32 *match, u32 *presc, u32 *cnt_ctrl, u32 *clk_hz)
{
    *interval = XTtcPs_GetInterval(&ttc);
    *match    = XTtcPs_GetMatchValue(&ttc, 0);
    *presc    = XTtcPs_GetPrescaler(&ttc);
    *cnt_ctrl = XTtcPs_ReadReg(ttc.Config.BaseAddress, XTTCPS_CNT_CNTRL_OFFSET);
    *clk_hz   = ttc.Config.InputClockHz;
}
