/*
 * interfaces.h - Prototipos de todos los módulos de Sep Hero.
 *
 * Es el contrato entre A (Martín, hardware) y B (compañero, lógica).
 * Cada bloque indica su DUEÑO. Si un módulo crece, se separa en su propio .h
 * sin cambiar las firmas. Para cambiar una firma, abrir un issue primero.
 *
 * Regla: game_engine.c, game_fsm.c y render.c solo usan funciones de este
 * archivo, nunca drivers de Xilinx. Así B los compila y prueba en el PC con
 * los stubs de tests/stubs.c.
 */
#ifndef INTERFACES_H
#define INTERFACES_H

#include "game_types.h"

/* ===================== Dueño B: IP debouncer_axi (AO3) ================== */
typedef struct {
    u16 deb_ms;        /* tiempo de antirrebote [ms]              */
    u8  lane_mask;     /* carriles habilitados (bit i = BTNi)     */
    u16 umbral_shake;  /* umbral del acelerómetro [mg] (para A)   */
} DebCfg;

typedef struct {
    uintptr_t base;    /* dirección base AXI (XPAR_..._BASEADDR)  */
    DebCfg    cfg;
} Debouncer;

int  debouncer_init(Debouncer *dev, const DebCfg *cfg);   /* 0 = OK          */
u8   debouncer_get_edges(Debouncer *dev);                 /* lee y limpia EDGE */
u8   debouncer_get_state(const Debouncer *dev);

/* uart_config.c: pide los parámetros por consola y los valida. */
int  uart_config_read(DebCfg *cfg);

/* ===================== Dueño B: lógica del juego (AC1, AC2) ============= */
void game_reset(GameState *gs);
/* Juzga un golpe contra el chart. Devuelve 1 = acierto, 0 = fallo. */
int  game_judge_hit(GameState *gs, const Song *song, const Hit *hit);
/* Avanza el tiempo: marca notas no tocadas como fallos, descuenta star power. */
void game_update(GameState *gs, const Song *song, u32 song_ms);
int  game_try_star_power(GameState *gs);   /* llamado al detectar shake */
int  game_is_over(const GameState *gs, const Song *song, u32 song_ms);

typedef enum {
    ST_INIT, ST_MENU, ST_LEVEL, ST_PLAY, ST_PAUSE, ST_GAMEOVER,
    ST_RECORDS, ST_CONFIG
} FsmState;

typedef struct GameCtx GameCtx;   /* definido en game_fsm.h (B) */
void fsm_step(GameCtx *ctx, UiEvent ev);

/* ===================== Dueño B: render.c ================================ */
void render_frame(const GameCtx *ctx);

/* ===================== Dueño A: periféricos ============================= */
int  sensors_init(void);
void sensors_sample(void);          /* llamado desde timer1_isr (10 ms)   */
int  sensors_shake(void);           /* 1 si |a| - 1 g > umbral            */
u8   sensors_crowd_level(void);     /* 0..100 a partir del micrófono      */
UiEvent sensors_joy_event(void);    /* UI_UP / UI_DOWN / UI_NONE          */

typedef struct { u16 lo_lux, hi_lux; } Opt3001Cfg;
int  opt3001_init(const Opt3001Cfg *cfg);   /* CONFIG, LOW/HIGH_LIMIT */
u16  opt3001_read_lux(void);                /* RESULT                 */
Theme theme_current(void);                  /* actualizado por theme_isr */

UiEvent ui_pop(void);               /* cola de eventos de S1/S2/joystick  */
int  hit_pop(Hit *out);             /* cola circular de golpes; 0 = vacía */

/* ===================== Audio: A = PWM, B = secuenciador ================= */
typedef struct {
    const Note *notes;
    u16 n, idx;
    u32 note_end_ms;
    u8  vol;            /* 0..100 % (duty) */
} Buzzer_t;

void buzzer_tone(Buzzer_t *bz, u16 f_hz, u8 vol);       /* A: TTC0       */
void buzzer_play(Buzzer_t *bz, const Song *song);       /* B             */
void buzzer_tick(Buzzer_t *bz, u32 song_ms);            /* B, 1 ms       */
void buzzer_stop(Buzzer_t *bz);

/* ===================== SD: A = FatFs, B = formatos ====================== */
int  sd_mount(void);                                            /* A */
int  sd_read_file(const char *path, char *buf, u32 max, u32 *len); /* A */
int  sd_write_file(const char *path, const char *buf, u32 len);    /* A */

int  song_parse(const char *buf, u32 len, Song *song, Note *pool, u16 max); /* B */
int  records_parse(const char *buf, u32 len, Record *r, u8 max);           /* B */
u32  records_format(const Record *r, u8 n, char *buf, u32 max);            /* B */

#endif /* INTERFACES_H */
