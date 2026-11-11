/*
 * interfaces.h - Prototipos de todos los módulos de Sep Hero (contrato entre A y B).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: diseño propio (docs/MANUAL_IMPLEMENTACION.md §7.4).
 *
 * VERSION AVANCE (sin pantalla, joystick, micrófono ni microSD).
 *
 * Regla de portabilidad: game_engine.c, game_fsm.c,
 * audio_seq.c, sd_formats.c, debouncer_logic.c y uart_config.c solo usan funciones
 * declaradas aquí, nunca drivers de Xilinx. Así se compilan y prueban en el PC
 * (tests/ y sim/) contra los stubs de tests/stubs.c.
 *
 * Cada bloque indica en qué archivo vive la implementación.
 */
#ifndef INTERFACES_H
#define INTERFACES_H

#include "game_types.h"

/* ====================================================================== */
/* IP propio debouncer_axi (AO3)                                          */
/* ====================================================================== */

/* Parámetros que el usuario entrega por la consola UART (SepHero_BD página 3). */
typedef struct {
    u16 deb_ms;        /* tiempo de antirrebote [ms]            -> IP (reg DEB_MS)   */
    u8  btn_mask;      /* bit0 S1, bit1 S2, bit2 JOY_SEL        -> IP (reg BTN_MASK) */
    u16 umbral_shake;  /* umbral del acelerómetro [mg]          -> sensors.c         */
    u8  led_pattern;   /* LEDs LD3..LD0 en modo claro [1-F]     -> IP (reg LED_CTRL) */
} DebCfg;

typedef struct {
    uintptr_t base;    /* dirección base AXI (XPAR_..._BASEADDR) */
    DebCfg    cfg;     /* última configuración aplicada          */
    u8        light;   /* 1 = modo claro habilitado (LEDs encendidos) */
} Debouncer;

/* debouncer_axi.c (no portable): inicializa y configura el hardware por punteros.
 * 0 = OK, -1 = puntero nulo, -2 = parámetros fuera de rango, -3 = read-back distinto. */
int  debouncer_init(Debouncer *dev, const DebCfg *cfg);
u8   debouncer_get_edges(Debouncer *dev);          /* lee EDGE y lo borra (W1C) */
u8   debouncer_get_state(const Debouncer *dev);    /* nivel filtrado            */
void debouncer_readback(const Debouncer *dev, u16 *deb_ms, u8 *btn_mask);  /* relee el IP */
void debouncer_set_theme(Debouncer *dev, int light);   /* LED_CTRL.THEME_LIGHT (modo claro) */
u8   debouncer_led_readback(const Debouncer *dev);     /* LED_CTRL releído: [4] tema, [3:0] patrón */

/* debouncer_logic.c (portable): validación y valores por defecto. */
#define DEB_MS_MIN        1u
#define DEB_MS_MAX        255u
#define SHAKE_MG_MIN      200u
#define SHAKE_MG_MAX      2000u
#define LED_PAT_MIN       1u
#define LED_PAT_MAX       0xFu
int  debcfg_validate(const DebCfg *c);             /* 0 = válida */
void debcfg_default(DebCfg *c);                    /* 20 ms, 0x7, 800 mg, 0xF */

/* ====================================================================== */
/* UART (AO3: configuración desde la consola del PC)                      */
/* ====================================================================== */

/* uart_hal.c (no portable). */
int  uart_hal_getc(void);                 /* -1 si no hay byte */
void uart_hal_puts(const char *s);

/* uart_config.c (portable): lector de línea no bloqueante + parser. */
typedef struct {
    u8     step;       /* 0..3: DEB_MS, BTN_MASK, UMBRAL_SHAKE, LED_PATTERN */
    u8     len;        /* caracteres acumulados en line[]                  */
    u8     last_eol;   /* último fin de línea visto ('\r' o '\n'; 0 = otro) */
    char   line[24];
    DebCfg tmp;        /* configuración en construcción                    */
} UartCfg;
void uart_cfg_begin(UartCfg *u, const DebCfg *current);
int  uart_cfg_poll(UartCfg *u, DebCfg *out);   /* 0 = en curso, 1 = listo */
/* Utilidades de formato sin printf (portables; las usan uart_config, fsm y diag). */
char *fmt_u32(char *dst, u32 v);               /* escribe decimal; devuelve el fin  */
char *fmt_hex(char *dst, u32 v, u8 digits);    /* escribe hex en mayúsculas         */
char *fmt_str(char *dst, const char *s);

/* ====================================================================== */
/* Relojes (isr.c, no portable; stubs en el PC)                           */
/* ====================================================================== */
u32  clock_sys_ms(void);              /* ms desde el arranque (siempre corre)  */
u32  clock_song_ms(void);             /* ms de canción (corre solo si run = 1) */
void clock_song_set(u32 ms);
void clock_song_run(int run);

/* ====================================================================== */
/* Entradas: botones de la Booster vía debouncer_axi (input.c, no portable) */
/* ====================================================================== */
/* input.c */
void    input_set_play_mode(int play);    /* 1 = JUEGO (flechas -> Hit); 0 = menús */
int     hit_pop(Hit *out);                /* 0 = cola vacía */
UiEvent ui_pop(void);
void    input_flush(void);                /* vacía ambas colas */

/* ====================================================================== */
/* Sensores: acelerómetro y micrófono (sensors.c, no portable)            */
/* ====================================================================== */
void sensors_set_shake_threshold(u16 mg);
u8   sensors_crowd_level(void);           /* sin micrófono en el avance: 0 */

/* ====================================================================== */
/* Sensor de luz y tema (opt3001.c, theme.c; no portables)                */
/* ====================================================================== */
typedef struct { u16 lo_lux, hi_lux; } Opt3001Cfg;
Theme theme_current(void);                /* actualizado por la interrupción del OPT3001 */

/* ====================================================================== */
/* Audio: audio.c = PWM (no portable), audio_seq.c = secuenciador (portable) */
/* ====================================================================== */
typedef struct {
    const Note *notes;      /* pista principal (canción o jingle)                 */
    u16 n, idx;
    u8  vol;                /* 0..100 % (duty)                                    */
    volatile u8 playing;    /* 1 = secuenciando                                   */
    volatile u8 paused;     /* 1 = pista principal en silencio (estado PAUSA)     */
    u8  loop;               /* jingles en loop (menú)                             */
    u8  song_clock;         /* 1 = usa song_ms (se pausa con la canción)          */
    u32 t_ms;               /* reloj propio del jingle (lo avanza buzzer_tick)    */
    u16 cur_f;              /* frecuencia sonando (evita reescribir el TTC)       */
    const Note *sfx;        /* efecto corto superpuesto a la pista principal      */
    u16 sfx_n, sfx_idx;
    volatile u8 sfx_on;
    u32 sfx_t;
} Buzzer_t;

void buzzer_tone(Buzzer_t *bz, u16 f_hz, u8 vol);                    /* audio.c (TTC0) */
void buzzer_init(Buzzer_t *bz, u8 vol);                              /* audio_seq.c    */
void buzzer_play(Buzzer_t *bz, const Song *song);                    /* canción        */
void buzzer_play_jingle(Buzzer_t *bz, const Note *notes, u16 n, u8 loop);
void buzzer_play_sfx(Buzzer_t *bz, const Note *notes, u16 n);
void buzzer_tick(Buzzer_t *bz, u32 song_ms);                         /* cada 1 ms (ISR) */
void buzzer_pause(Buzzer_t *bz, int on);
void buzzer_stop(Buzzer_t *bz);

/* Jingles y efectos (audio_seq.c). */
extern const Note JINGLE_MENU[];  extern const u16 JINGLE_MENU_N;
extern const Note JINGLE_WIN[];   extern const u16 JINGLE_WIN_N;
extern const Note JINGLE_LOSE[];  extern const u16 JINGLE_LOSE_N;
extern const Note SFX_COUNT[];    extern const u16 SFX_COUNT_N;
extern const Note SFX_GO[];       extern const u16 SFX_GO_N;
extern const Note SFX_STAR[];     extern const u16 SFX_STAR_N;
extern const Note SFX_ENCORE[];   extern const u16 SFX_ENCORE_N;

/* ====================================================================== */
/* SD: sd_storage.c = FatFs (no portable), sd_formats.c = formatos (portable) */
/* ====================================================================== */
#define N_SONGS 2u                         /* SONG1.SHC y SONG2.SHC */
typedef struct { u8 mounted; } SdCtx;
int  sd_init(SdCtx *sd);                                             /* intenta montar */
int  sd_load_song(SdCtx *sd, Song *song, Note *pool, u8 song_idx);   /* n notas o < 0  */
int  sd_load_records(SdCtx *sd, Record *r, u8 max);                  /* devuelve n     */
int  sd_save_records(SdCtx *sd, const Record *r, u8 n);

int  song_parse(const char *buf, u32 len, Song *song, Note *pool, u16 max);
int  records_parse(const char *buf, u32 len, Record *r, u8 max);
u32  records_format(const Record *r, u8 n, char *buf, u32 max);
int  records_insert(Record *r, u8 *n, const char *name, u32 score);  /* posición o -1 */

/* song_embedded.c (generado por tools/shc2c.py): respaldo sin microSD. */
extern const char  g_song1_shc[];  extern const u32 g_song1_shc_len;
extern const char  g_song2_shc[];  extern const u32 g_song2_shc_len;

/* ====================================================================== */
/* Lógica del juego (game_engine.c, portable; AC2)                        */
/* ====================================================================== */
void game_reset(GameState *gs);
int  game_judge_hit(GameState *gs, const Song *song, const Hit *hit);  /* 1 = acierto */
void game_update(GameState *gs, const Song *song, u32 song_ms);
int  game_try_star_power(GameState *gs);                               /* 1 = activado */
int  game_is_over(const GameState *gs, const Song *song, u32 song_ms); /* GameResult   */
u32  song_duration_ms(const Song *song);

/* ====================================================================== */
/* Máquina de estados (game_fsm.c, portable; AC1)                         */
/* ====================================================================== */
typedef enum {
    ST_INIT, ST_MENU, ST_LEVEL, ST_PLAY, ST_PAUSE, ST_GAMEOVER,
    ST_RECORDS, ST_CONFIG
} FsmState;

typedef struct GameCtx GameCtx;   /* definido en game_fsm.h */
void fsm_start(GameCtx *ctx);
void fsm_step(GameCtx *ctx, UiEvent ev);

#endif /* INTERFACES_H */
