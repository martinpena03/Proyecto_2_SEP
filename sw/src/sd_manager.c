/*
 * sd_manager.c - Canciones y récords del avance (sin microSD).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio; reemplaza a
 *   sd_manager.c del proyecto completo (rama proyecto-final), que usa la microSD.
 *
 * Implementa la interfaz sd_* de interfaces.h con los charts embebidos (sd_manager_songs.c,
 * generados por tools/shc2c.py) y los récords solo en RAM.
 */
#include "xil_printf.h"
#include "interfaces.h"

static Record g_rec[MAX_RECORDS];
static u8 g_nrec;

int sd_init(SdCtx *sd)
{
    sd->mounted = 0;
    xil_printf("[SONG] avance sin microSD: canciones embebidas y records en RAM\r\n");
    return 0;
}

int sd_load_song(SdCtx *sd, Song *song, Note *pool, u8 song_idx)
{
    int n;
    (void)sd;
    if (song_idx == 0u) n = song_parse(g_song1_shc, g_song1_shc_len, song, pool, MAX_NOTES);
    else                n = song_parse(g_song2_shc, g_song2_shc_len, song, pool, MAX_NOTES);
    xil_printf("[SONG] cancion %d embebida: %d notas\r\n", song_idx + 1, n);
    return n;
}

int sd_load_records(SdCtx *sd, Record *r, u8 max)
{
    u8 i;
    (void)sd;
    for (i = 0; i < g_nrec && i < max; i++) r[i] = g_rec[i];
    return i;
}

int sd_save_records(SdCtx *sd, const Record *r, u8 n)
{
    u8 i;
    (void)sd;
    for (i = 0; i < n && i < MAX_RECORDS; i++) g_rec[i] = r[i];
    g_nrec = i;
    return 0;
}
