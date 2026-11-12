/*
 * game_types.h - Tipos y constantes compartidos de Sep Hero (contrato entre módulos).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero (Martín Peña y compañero).
 * Fuente: diseño propio (SepHero_BD.pdf, páginas 1, 3 y 4, y docs/MANUAL_IMPLEMENTACION.md §7.4).
 *
 * Portable: NO incluir headers de Xilinx aquí. Este archivo se compila también en el PC
 * (tests/ y sim/) con gcc. Cubre la actividad AC2 (estructuras, arreglos, enums y macros).
 */
#ifndef GAME_TYPES_H
#define GAME_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* Mismos nombres y tipos base que xil_types.h del BSP: en la placa ambas definiciones
 * coexisten porque son idénticas (C11 permite repetir un typedef igual). */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;

/* ------------------------------------------------------------------ */
/* Parámetros del juego                                               */
/* ------------------------------------------------------------------ */
#define N_LANES           4u       /* carriles: <- ^ v -> (direcciones del joystick)     */
#define HIT_WIN_MS        200u     /* |t_golpe - t_nota| máximo para un acierto (ampliado
                                    * de 120 a 200 ms tras probar en la placa)           */
#define PTS_NOTA          100u     /* puntos por nota (x2 con star power)                */
#define STAR_STREAK       10u      /* aciertos seguidos que llenan la barra de poder     */
#define STAR_MS           20000u   /* duración del star power                            */
#define ROCK_MAX          20       /* tope del rock meter                                */
#define ROCK_START        10       /* rock meter inicial (mitad de ROCK_MAX)             */
#define ROCK_HIT          (+1)     /* acierto                                            */
#define ROCK_MISS         (-2)     /* nota que pasa el receptor sin golpe                */
#define ROCK_WRONG        (-2)     /* flecha equivocada con una nota en la ventana       */
#define MAX_NOTES         512u     /* capacidad del pool de notas                        */
#define MAX_RECORDS       5u       /* top 5                                              */

#define AVANCE_PLAY_MS    20000u   /* avance: duración fija de PLAY (cuenta regresiva)   */
#define LEADIN_MS         3000u    /* cuenta 3-2-1: la 1.a nota del chart cae >= 3 s     */
#define SONG_TAIL_MS      2000u    /* margen tras la última nota para declarar el fin    */
#define NOTE_LOOKAHEAD_MS 1500u    /* tiempo que tarda una nota en recorrer la pista     */

#define TICK1_MS          2u       /* periodo de axi_timer_1                             */
#define SENS_DIV          5u       /* acelerómetro cada 5 ticks = 10 ms                  */
#define FRAME_DIV         15u      /* paso de la FSM cada 15 ticks = 30 ms               */
#define OPT_POLL_DIV      100u     /* OPT3001 cada 100 ticks = 200 ms                    */
#define HIT_Q_LEN         16u      /* cola de golpes (potencia de 2)                     */
#define UI_Q_LEN          8u       /* cola de eventos de interfaz (potencia de 2)        */

#define ENCORE_WIN_MS     5000u    /* ventana del grito en GAME OVER                     */
#define ENCORE_LEVEL      70u      /* nivel de público (0..100) que cuenta como grito    */
#define ENCORE_FRAMES     3u       /* frames seguidos sobre ENCORE_LEVEL                 */
#define ENCORE_BONUS_PCT  10u      /* +10 % del puntaje                                  */

/* ------------------------------------------------------------------ */
/* Tipos                                                              */
/* ------------------------------------------------------------------ */

/* Carriles = direcciones del joystick, de izquierda a derecha en la pantalla. */
typedef enum { IZQ = 0, ARRIBA = 1, ABAJO = 2, DER = 3 } Lane;

/* Una nota del chart (viene de SONGn.SHC). Sirve a la vez para el juego y para el buzzer. */
typedef struct {
    u32  t_ms;   /* instante en que la nota cruza el receptor */
    Lane lane;   /* IZQ..DER                                  */
    u16  freq;   /* Hz para el buzzer (0 = silencio)          */
    u16  dur;    /* duración del tono en ms                   */
} Note;

typedef struct {
    char  name[16];
    u16   bpm;
    u16   n;        /* cantidad de notas en chart */
    Note *chart;    /* apunta al pool del GameCtx */
} Song;

typedef enum { THEME_LIGHT = 0, THEME_DARK = 1 } Theme;

/* Sub-FSM del star power (SepHero_BD página 3). */
typedef enum { STAR_CHARGING = 0, STAR_READY = 1, STAR_ACTIVE = 2 } StarPhase;

typedef struct {
    u32 score;
    u16 streak, max_streak;
    u8  star_bar;              /* 0..100 % (para el HUD)                            */
    u8  star_phase;            /* StarPhase                                         */
    u8  charge;                /* aciertos seguidos desde el último reinicio        */
    volatile u32 star_ms;      /* ms restantes de star power (lo descuenta timer0)  */
    s8  rock;                  /* rock meter; 0 => GAME OVER                        */
    u8  theme;                 /* Theme                                             */
    u8  crowd;                 /* nivel del público 0..100 (micrófono)              */
    u16 next_note;             /* primera nota sin juzgar                           */
    u16 hits, misses;
    u8  judged[(MAX_NOTES + 7u) / 8u];   /* bitset: nota i ya juzgada               */
    u8  rcv_fx[N_LANES];       /* frames de destello que le quedan al receptor      */
    u8  rcv_fx_kind[N_LANES];  /* 1 = acierto, 2 = fallo                            */
    u8  encore_done;
} GameState;

typedef enum { RESULT_NONE = 0, RESULT_WIN = 1, RESULT_LOSE = 2 } GameResult;

/* Golpe: flecha del joystick con su sello de tiempo (lo encola timer1_isr). */
typedef struct {
    u8  lane;      /* Lane */
    u32 song_ms;
} Hit;

typedef struct {
    char name[4];   /* 3 letras + '\0' */
    u32  score;
} Record;

/* Eventos de interfaz. Las flechas del juego NO pasan por aquí (van a la cola de Hit). */
typedef enum {
    UI_NONE = 0, UI_OK, UI_BACK, UI_UP, UI_DOWN, UI_SHAKE
} UiEvent;

/* Macros de apoyo para el bitset judged[] (AC2). */
#define JUDGED_GET(gs, i)  (((gs)->judged[(i) >> 3] >> ((i) & 7u)) & 1u)
#define JUDGED_SET(gs, i)  ((gs)->judged[(i) >> 3] |= (u8)(1u << ((i) & 7u)))

#endif /* GAME_TYPES_H */
