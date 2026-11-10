/*
 * game_fsm.h - Contexto global del juego (GameCtx) y accesores.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §9.2).
 *
 * Portable. GameCtx reúne todo el estado del juego en una sola estructura que main
 * declara como 'static' (unos 7 KB: no cabe en el stack) y que el resto de los módulos
 * recibe por puntero (AC2).
 */
#ifndef GAME_FSM_H
#define GAME_FSM_H

#include "interfaces.h"

#define PREVIEW_N   8u      /* notas del preview en la selección de canción */
#define MENU_ITEMS  3u      /* Jugar, Récords, Configurar                   */
#define PAUSE_ITEMS 3u      /* Continuar, Reiniciar, Salir                  */

struct GameCtx {
    FsmState  st, prev_st;          /* estado actual y anterior                       */
    GameState gs;                   /* estado de la partida                           */
    Song      song;                 /* canción cargada (song.chart apunta a pool)     */
    Note      pool[MAX_NOTES];      /* almacenamiento de las notas                    */
    Note      preview[PREVIEW_N];   /* primeras notas, con el tiempo relativo a 0     */
    Record    rec[MAX_RECORDS];     /* top 5                                          */
    u8        n_rec;
    DebCfg    cfg;                  /* configuración vigente (UART)                   */
    Debouncer deb;                  /* instancia del IP propio                        */
    UartCfg   ucfg;                 /* diálogo de configuración en curso              */
    Buzzer_t  bz;                   /* secuenciador del buzzer                        */
    SdCtx     sd;                   /* estado de la microSD                           */
    u8   cursor;                    /* opción resaltada en los menús                  */
    u8   song_sel;                  /* canción elegida (0..N_SONGS-1)                 */
    u8   song_ok;                   /* 1 = la canción elegida cargó bien              */
    u8   result;                    /* GameResult de la última partida                */
    u32  t_state;                   /* clock_sys_ms() al entrar al estado             */
    char name[4];                   /* iniciales en edición ("AAA")                   */
    u8   name_pos;                  /* letra que se está editando (0..2)              */
    u8   name_entry;                /* 1 = el puntaje entra al top 5 y falta el nombre */
    s8   rec_pos;                   /* posición del récord recién guardado (-1 = no)  */
    u8   encore_cnt;                /* frames seguidos con grito                      */
    u8   count_shown;               /* última cifra de la cuenta 3-2-1 anunciada      */
    u8   redraw_seq;                /* cambia cuando hay que redibujar toda la pantalla */
    int  cfg_status;                /* resultado del último debouncer_init            */
};

/* Accesores usados por las ISR (isr.c), que no necesitan conocer el resto del contexto. */
static inline GameState *ctx_gs(GameCtx *c) { return &c->gs; }
static inline Buzzer_t  *ctx_bz(GameCtx *c) { return &c->bz; }

/* Aplica una configuración: IP propio + umbrales de joystick y acelerómetro. */
int fsm_apply_cfg(GameCtx *ctx, const DebCfg *cfg);

#endif /* GAME_FSM_H */
