/*
 * main.c - Punto de entrada de Sep Hero, VERSION AVANCE: inicialización y bucle principal.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: diseño propio (docs/MANUAL_IMPLEMENTACION.md §9.6). El orden de inicialización
 *   de ADC / I2C sigue el main.c del Proyecto_Base_Booster (LAB10).
 *
 * El avance no dibuja nada: todo se muestra por la UART y con los LEDs LD0..LD3.
 *   AO2: interrupciones de axi_timer_0/1; el acelerómetro se lee en la ISR de timer1
 *        ("Movimiento detectado") y el OPT3001 se revisa cada 200 ms ("Mucha luz").
 *   AO3: IP propio debouncer_axi (botones + LEDs del modo claro), configurado por UART
 *        con debouncer_init(Debouncer *, const DebCfg *).
 *   AC1: máquina de estados; cada estado se anuncia por consola.
 *   AC6: melodía del menú en el buzzer (TTC0); JOY_SEL la silencia o la reanuda.
 * Los niveles solo existen en la máquina de estados (sin música): PLAY imprime
 * "Juego iniciado" y una cuenta regresiva de 20 s antes de pasar a GAMEOVER.
 */
#include "xparameters.h"
#include "xil_printf.h"
#include "hw.h"
#include "lib/platform.h"
#include "lib/ADC.h"
#include "lib/I2C.h"

#define OPT_LO_LUX  40u     /* bajo esto: poca luz -> modo oscuro */
#define OPT_HI_LUX  80u     /* sobre esto: mucha luz -> modo claro */

static GameCtx ctx;         /* global: ~7 KB, fuera del stack */

/* Imprime el resultado de un paso de inicialización. */
static void report(const char *what, int code)
{
    if (code == 0) xil_printf("[INIT] %s OK\r\n", what);
    else           xil_printf("[INIT] %s FALLO (%d)\r\n", what, code);
}

/* Inicializa el hardware del avance. El GIC va al final para que ninguna interrupción
 * corra con un periférico sin inicializar. */
static void init_all(GameCtx *c)
{
    static const Opt3001Cfg optcfg = { OPT_LO_LUX, OPT_HI_LUX };
    int r;

    init_platform();
    r = audio_hw_init();                             /* lo primero: buzzer en silencio */
    xil_printf("\r\n=== SEP HERO :: AVANCE === %s %s  (mudo=%d)\r\n", __DATE__, __TIME__, audio_is_muted());
    report("buzzer TTC0", r);

    /* ADC SPI: acelerómetro (calibración en reposo). */
    r = (init_adc(&SpiInstance1, SPI_DEVICE_ID_1) == XST_SUCCESS) ? 0 : -1;
    report("ADC SPI", r);
    report("acelerometro", sensors_acc_init());

    /* I2C + sensor de luz. Si falla, el tema queda fijo en oscuro. */
    r = (init_IIC() == XST_SUCCESS) ? 0 : -1;
    report("I2C", r);
    if (r == 0) r = opt3001_init(&optcfg);
    report("OPT3001", r);
    theme_init(r == 0, &optcfg);

    /* IP propio con la configuración por defecto (se cambia desde CONFIG por la UART). */
    debcfg_default(&c->cfg);
    report("IP Debouncer_IP", fsm_apply_cfg(c, &c->cfg));

    buzzer_init(&c->bz, 50);
    report("timers + GIC", isr_init(c));
}

/* AO2: el acelerómetro no tiene pin de interrupción (salida analógica). La detección
 * corre dentro de la ISR de axi_timer_1 (sensors_acc_sample, cada 10 ms) y aquí solo se
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

/* AO2 / AO3: aviso por consola de cada interrupción de Debouncer_IP (irq_deb, GIC 63).
 * La ISR cuenta las pulsaciones ya filtradas (g_btn_cnt) y aquí se imprimen. */
static void btn_task(void)
{
    static const char *const NAME[3] = { "S1 -> OK", "S2 -> atras / pausa", "JOY_SEL -> musica ON/OFF" };
    static u8 seen[3];
    int i;
    for (i = 0; i < 3; i++) {
        u8 n = g_btn_cnt[i];
        if (n != seen[i]) {
            seen[i] = n;
            xil_printf("\r\n*** [IRQ] Debouncer_IP (GIC 63): %s *** irq #%d\r\n", NAME[i], (int)g_irq_cnt[2]);
        }
    }
}

int main(void)
{
    init_all(&ctx);
    xil_printf("\r\n--- AVANCE: AO2 (luz + acelerometro por interrupcion de timer), AO3 (Debouncer_IP + LEDs del modo claro),\r\n"
               "    AC1 (maquina de estados por consola), AC6 (melodia del menu) ---\r\n"
               "    Luz: oscuro < %d lux, claro > %d lux. Botones: S1 = OK, S2 = atras, JOY_SEL = musica ON/OFF. 'h' = ayuda.\r\n"
               "    Los niveles no tienen musica: PLAY dura 20 s con cuenta regresiva por consola.\r\n",
               OPT_LO_LUX, OPT_HI_LUX);
    fsm_start(&ctx);                             /* canciones embebidas y estado INIT */

    while (1) {
        theme_task();                            /* OPT3001 por I2C cada 200 ms (AO2) */
        motion_task();                           /* aviso del acelerómetro (AO2)      */
        btn_task();                              /* aviso de los botones (irq_deb)    */
        audio_user_mute_task();                  /* JOY_SEL: música ON / OFF          */
        console_poll(&ctx);                      /* teclado del PC                    */
        if (g_frame_flag) {                      /* cada 30 ms (axi_timer_1)          */
            UiEvent ev;
            g_frame_flag = 0;
            ev = ui_pop();
            if (ev == UI_NONE) ev = console_pop_event();
            fsm_step(&ctx, ev);
            console_frame(&ctx);
        }
    }
    return 0;
}
