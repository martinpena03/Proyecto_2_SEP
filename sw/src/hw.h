/*
 * hw.h - Prototipos de la capa de hardware de Sep Hero (solo para la placa).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio.
 *
 * Complementa a interfaces.h con lo que NO necesita la lógica portable: inicializaciones,
 * rutinas de interrupción y funciones de diagnóstico. Lo incluyen main.c, isr.c, input.c,
 * sensors.c, opt3001.c, theme.c, audio.c, lcd_fast.c, sd_storage.c y diag.c.
 */
#ifndef HW_H
#define HW_H

#include "game_fsm.h"

/* ---- Ajustes dependientes de la placa (se confirman en las pruebas físicas) ---- */
#ifndef JOY_INVERT_Y
#define JOY_INVERT_Y   0     /* 1 si con la palanca hacia arriba el ADC entrega valores bajos */
#endif
#ifndef OPT_INT_BIT
#define OPT_INT_BIT    0     /* bit de axi_gpio_1 al que llega el INT del OPT3001           */
#endif
#define OPT_CAND_N     17u   /* candidatos del INT en axi_gpio_1 (02_build_bd.tcl, OPT_CAND) */
#define OPT_BIT_NONE   0xFFu /* INT no detectado: respaldo por timer                         */
#ifndef SEP_MUTE
#define SEP_MUTE       0     /* 1 = el firmware arranca con el buzzer deshabilitado          */
#endif

/* ---- isr.c: GIC, timers AXI y relojes -------------------------------------- */
int  isr_init(GameCtx *ctx);              /* 0 = OK. Deja las interrupciones habilitadas */
extern volatile u8  g_frame_flag;         /* 1 cada 30 ms; lo consume main               */
extern volatile u32 g_irq_cnt[4];         /* cuentas: timer0, timer1, debouncer, gpio    */
extern volatile u32 g_t1_max_us;          /* duración máxima de timer1_cb [us]           */

/* ---- input.c: joystick, botones y OPT_INT ---------------------------------- */
int  input_init(void);                    /* calibra el centro del joystick; 0 = OK      */
int  gpio1_init(u8 opt_bit);              /* axi_gpio_1 (OPT_INT) con interrupción       */
void joy_sample(void);                    /* desde timer1 (2 ms)                         */
void deb_isr(void *ref);                  /* interrupción del IP propio                  */
void gpio_isr(void *ref);                 /* interrupción de axi_gpio_1                  */
void ui_push(UiEvent ev);                 /* encola un evento de interfaz                */
void hit_push(u8 lane, u32 song_ms);      /* encola un golpe (también lo usa diag)       */
void input_mute_hits_until(u32 sys_ms);   /* descarta golpes hasta ese instante          */
u32  gpio1_read(void);                    /* nivel actual de las entradas de axi_gpio_1  */
void input_get_joy(u16 *x, u16 *y, u16 *x0, u16 *y0);   /* últimas lecturas y centro     */
void input_set_joy_inv_y(u8 inv);
extern volatile u8 opt_pending;           /* 1 = el OPT3001 pidió atención (INT en bajo) */
extern u8 g_opt_bit;                      /* bit de axi_gpio_1 conectado al INT          */

/* ---- sensors.c: acelerómetro y micrófono ----------------------------------- */
int  sensors_init(void);                  /* calibra en reposo; 0 = OK                   */
void sensors_sample(void);                /* desde timer1 (10 ms)                        */
void sensors_feed_mic(u16 sample);        /* muestra del micrófono (2 ms)                */
void sensors_get_debug(u16 *acc, u16 *cpg, u16 *mic_p2p, u16 *mic_noise, u32 *shakes);
u32  sensors_motion_count(void);          /* movimientos detectados por la ISR (AO2)     */
u16  sensors_last_motion_mg(void);        /* |a| - 1 g del último movimiento [mg]        */

/* ---- opt3001.c / theme.c: sensor de luz ------------------------------------ */
int  opt3001_init(const Opt3001Cfg *cfg); /* 0 = OK, < 0 = sin sensor                    */
u8   opt3001_detect_int(void);            /* bit de axi_gpio_1 con el INT o OPT_BIT_NONE */
const char *opt_cand_name(u8 bit);        /* "JA3 (K16)" etc.                            */
u16  opt3001_read_lux(void);
void opt3001_arm(Theme t);                /* fija LOW/HIGH_LIMIT según el tema           */
u16  lux_to_reg(u32 lux);
void theme_init(int sensor_ok, const Opt3001Cfg *cfg);
void theme_task(void);                    /* desde main: atiende opt_pending por I2C     */
extern volatile u32 g_theme_changes;

/* ---- audio.c: PWM del buzzer por TTC0 -------------------------------------- */
int  audio_hw_init(void);                 /* deja el buzzer en silencio                  */
void audio_set_mute(int mute);            /* 1 = salida deshabilitada (pin quieto)       */
int  audio_is_muted(void);
void audio_get_regs(u32 *interval, u32 *match, u32 *presc, u32 *cnt_ctrl, u32 *clk_hz);

/* ---- lcd_fast.c -------------------------------------------------------------- */
void lcd_fast_init(void);                 /* toma los offsets del driver del curso       */
void lcd_set_offsets(u8 xoff, u8 yoff);
void lcd_get_offsets(u8 *xoff, u8 *yoff);

/* ---- sd_storage.c ------------------------------------------------------------ */
int  sd_read_file(const char *path, char *buf, u32 max, u32 *len);
int  sd_write_file(const char *path, const char *buf, u32 len);

/* ---- diag.c: consola de diagnóstico (solo con -DSEP_DIAG) -------------------- */
void diag_poll(GameCtx *ctx);             /* atiende un comando de la UART, sin bloquear */
void diag_frame(GameCtx *ctx, u32 fsm_us, u32 render_us);
UiEvent diag_pop_event(void);             /* evento inyectado por la consola             */

#endif /* HW_H */
