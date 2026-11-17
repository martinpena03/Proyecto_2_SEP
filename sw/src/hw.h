/*
 * hw.h - Prototipos de la capa de hardware de Sep Hero, VERSION AVANCE (solo placa).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio.
 *
 * Complementa a interfaces.h con lo que NO necesita la lógica portable. En el avance la
 * placa usa solo: OPT3001 (I2C), acelerómetro (ADC SPI), Debouncer_IP (botones + LEDs),
 * buzzer (TTC0), 2 timers AXI y la UART. Sin pantalla, joystick, micrófono ni microSD.
 */
#ifndef HW_H
#define HW_H

#include "state_machine.h"

#ifndef SEP_MUTE
#define SEP_MUTE       0     /* 1 = el firmware arranca con el buzzer deshabilitado          */
#endif
#ifndef MUSIC_START_MUTED
#define MUSIC_START_MUTED 1  /* 1 = la música del juego parte en silencio; JOY_SEL la enciende/apaga */
#endif

/* ---- Aviso de temperatura (AO2, temperature_aconditioner.c) ---- */
#ifndef TEMP_ALERT_C
#define TEMP_ALERT_C   29    /* °C: al llegar a este valor se imprime la temperatura          */
#endif
#define TEMP_HYST_CC   50    /* centésimas de °C: se rearma al bajar de TEMP_ALERT_C - 0,5    */
#define SENS_CAND_N    8u    /* candidatos de SENS_INT en axi_gpio_1: Pmod JA (02_build_bd.tcl) */
#define SENS_BIT_NONE  0xFFu /* pin no detectado                                            */

/* ---- isr_handler.c: GIC, timers AXI y relojes -------------------------------------- */
int  isr_init(GameCtx *ctx);              /* 0 = OK. Deja las interrupciones habilitadas */
extern volatile u8  g_frame_flag;         /* 1 cada 30 ms; lo consume main               */
extern volatile u8  g_opt_poll;           /* 1 cada 200 ms: revisar el OPT3001 (AO2)     */
extern volatile u32 g_irq_cnt[4];         /* cuentas: timer0, timer1, debouncer, gpio    */
extern volatile u32 g_t1_max_us;          /* duración máxima de timer1_cb [us]           */

/* ---- isr_handler.c: ISR de Debouncer_IP (irq_deb) y axi_gpio_1 (irq_gpio) ------- */
void deb_isr(void *ref);                  /* interrupción del IP propio                  */
int  gpio1_init(void);                    /* axi_gpio_1 (SENS_INT) con interrupción      */
void gpio_isr(void *ref);                 /* interrupción de axi_gpio_1 (DRDY del TMP006) */
u32  gpio1_read(void);                    /* nivel actual de las entradas de axi_gpio_1  */
extern volatile u8  tmp_pending;          /* 1 = el TMP006 terminó una conversión (DRDY) */
extern volatile u32 g_tmp_irq;            /* interrupciones de DRDY atendidas por la ISR */
extern u8 g_tmp_bit;                      /* bit de axi_gpio_1 conectado al DRDY         */
extern volatile u8  g_mute_req;           /* pulsaciones de JOY_SEL (música ON / OFF)    */
extern volatile u8  g_btn_cnt[3];         /* pulsaciones vistas por la ISR: S1, S2, JOY_SEL */

/* ---- event_queues.c: colas ISR -> main ------------------------------------------- */
void ui_push(UiEvent ev);                 /* encola un evento de interfaz                */
void hit_push(u8 lane, u32 song_ms);      /* encola un golpe (autoplay de la consola)    */
void input_mute_hits_until(u32 sys_ms);   /* descarta golpes hasta ese instante          */

/* ---- temperature_aconditioner.c: TMP006 (interrupción del pin DRDY, AO2) ----------------- */
int  tmp006_init(void);                   /* ID + CONFIG con DRDY habilitado; 0 = OK     */
u8   tmp006_detect_drdy(u8 skip_bit);     /* bit de axi_gpio_1 con el DRDY o SENS_BIT_NONE */
const char *sens_cand_name(u8 bit);       /* "JA3 (K16)" etc.                            */
s32  tmp006_read_cc(void);                /* temperatura en centésimas de °C (libera DRDY) */
s32  tmp006_last_cc(void);                /* última lectura, sin usar el I2C             */
void temp_task(void);                     /* desde main: atiende tmp_pending por I2C     */

/* ---- accelerometer_aconditioner.c: acelerómetro ----------------------------------------------- */
int  sensors_acc_init(void);                /* calibra en reposo; 0 = OK                   */
void sensors_acc_sample(void);              /* desde timer1 (10 ms)                        */
void sensors_acc_debug(u16 *acc, u16 *cpg, u32 *shakes);
u32  sensors_motion_count(void);          /* movimientos detectados por la ISR (AO2)     */
u16  sensors_last_motion_mg(void);        /* |a| - 1 g del último movimiento [mg]        */

/* ---- light_aconditioner.c: sensor de luz ---------------------------------------------- */
int  opt3001_init(const Opt3001Cfg *cfg); /* 0 = OK, < 0 = sin sensor                    */
u16  opt3001_read_lux(void);
void opt3001_arm(Theme t);                /* fija LOW/HIGH_LIMIT según el tema           */
u16  lux_to_reg(u32 lux);
void theme_init(int sensor_ok, const Opt3001Cfg *cfg);
void theme_task(void);                    /* desde main: atiende g_opt_poll por I2C      */
extern volatile u32 g_theme_changes;

/* ---- ttc0_pwm.c: PWM del buzzer por TTC0 -------------------------------------- */
int  audio_hw_init(void);                 /* deja el buzzer en silencio                  */
void audio_set_mute(int mute);            /* 1 = salida deshabilitada (pin quieto)       */
int  audio_is_muted(void);
int  audio_user_muted(void);              /* 1 = el usuario silenció con JOY_SEL         */
void audio_user_mute_task(void);          /* desde main: atiende las pulsaciones de JOY_SEL */
void audio_get_regs(u32 *interval, u32 *match, u32 *presc, u32 *cnt_ctrl, u32 *clk_hz);

/* ---- uart_console.c: consola del avance por UART ---------------------------------- */
void    console_poll(GameCtx *ctx);       /* atiende una tecla, sin bloquear             */
void    console_frame(GameCtx *ctx);      /* autoplay y cierre de partida, una vez por frame */
UiEvent console_pop_event(void);          /* evento inyectado por teclado                */

#endif /* HW_H */
