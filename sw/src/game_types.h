/*
 * game_types.h - Tipos compartidos de Sep Hero (contrato entre A y B).
 * Dueño: B. Cambios: avisar a A antes (lo usa render, isr y sd_storage).
 * Portable: NO incluir headers de Xilinx aquí (se compila también en el PC).
 */
#ifndef GAME_TYPES_H
#define GAME_TYPES_H

#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;

/* --- Parámetros del juego --- */
#define N_LANES        4u      /* BTN0..BTN3 = 4 carriles               */
#define HIT_WIN_MS     120u    /* ventana para considerar un acierto    */
#define PTS_NOTA       100u
#define STAR_STREAK    10u     /* racha que llena la barra de poder     */
#define STAR_MS        20000u  /* duración del star power (20 s)        */
#define ROCK_MAX       20
#define ROCK_HIT       (+1)
#define ROCK_MISS      (-2)
#define MAX_NOTES      512u
#define MAX_RECORDS    5u

/* Una nota del chart (viene de SONG1.SHC). */
typedef struct {
    u32 t_ms;   /* instante en que la nota cruza la línea de golpe */
    u8  lane;   /* 0..3                                            */
    u16 freq;   /* Hz para el buzzer (0 = silencio)                */
    u16 dur;    /* ms                                              */
} Note;

typedef struct {
    char  name[16];
    u16   bpm;
    u16   n;        /* cantidad de notas en chart */
    Note *chart;
} Song;

typedef enum { THEME_LIGHT = 0, THEME_DARK = 1 } Theme;

typedef struct {
    u32 score;
    u16 streak;
    u8  star_bar;   /* 0..100 %                      */
    u32 star_ms;    /* ms restantes de star power    */
    s8  rock;       /* rock meter; 0 => GAME OVER    */
    u8  theme;      /* Theme                         */
    u8  crowd;      /* nivel del público (micrófono) */
} GameState;

/* Golpe de traste registrado por la ISR del debouncer. */
typedef struct {
    u8  lane;
    u32 song_ms;
} Hit;

typedef struct {
    char name[4];   /* 3 letras + '\0' */
    u32  score;
} Record;

/* Eventos de interfaz (S1, S2, joystick). */
typedef enum {
    UI_NONE = 0, UI_OK, UI_BACK, UI_UP, UI_DOWN, UI_SHAKE
} UiEvent;

#endif /* GAME_TYPES_H */
