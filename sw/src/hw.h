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

/* ---- isr_handler.c: GIC, timers AXI y relojes -------------------------------------- */
int  isr_init(GameCtx *ctx);              /* 0 = OK. Deja las interrupciones habilitadas */
extern volatile u8  g_frame_flag;         /* 1 cada 30 ms; lo consume main               */
extern volatile u8  g_opt_poll;           /* 1 cada 200 ms: revisar el OPT3001 (AO2)     */
extern volatile u32 g_irq_cnt[3];         /* cuentas: timer0, timer1, debouncer          */
extern volatile u32 g_t1_max_us;          /* duración máxima de timer1_cb [us]           */

/* ---- isr_handler.c: ISR de Debouncer_IP (irq_deb) ------------------------------- */
void deb_isr(void *ref);                  /* interrupción del IP propio                  */

/* ---- event_queues.c: colas ISR -> main ------------------------------------------- */
void ui_push(UiEvent ev);                 /* encola un evento de interfaz                */
void hit_push(u8 lane, u32 song_ms);      /* encola un golpe (autoplay de la consola)    */
void input_mute_hits_until(u32 sys_ms);   /* descarta golpes hasta ese instante          */

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
void audio_get_regs(u32 *interval, u32 *match, u32 *presc, u32 *cnt_ctrl, u32 *clk_hz);

/* ---- uart_console.c: consola del avance por UART ---------------------------------- */
void    console_poll(GameCtx *ctx);       /* atiende una tecla, sin bloquear             */
void    console_frame(GameCtx *ctx);      /* autoplay y cierre de partida, una vez por frame */
UiEvent console_pop_event(void);          /* evento inyectado por teclado                */

#endif /* HW_H */
