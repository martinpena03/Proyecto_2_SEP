/*
 * sd_manager.c - Canciones y récords del avance: microSD con FatFs (xilffs) y respaldo embebido.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio con ayuda de Claude.
 *
 * Implementa la interfaz sd_* de interfaces.h con los charts embebidos (sd_manager_songs.c,
 * generados por tools/shc2c.py: nivel Demo, SONG1 y SONG2) y, si hay tarjeta, con la microSD.
 *
 * Fuente: uso de xilffs visto en la clase del 02-11-2026 ("memoria microSD y FAT en
 *   Vitis": SD0 del PS, f_mount / f_open / f_read / f_write, nombres 8.3, ruta "0:/");
 *   implementación propia (docs/MANUAL_IMPLEMENTACION.md §10).
 *
 * Comportamiento:
 *   - Con tarjeta FAT32: lee 0:/SONGn.SHC (si falta, usa el chart embebido) y lee /
 *     escribe 0:/RECORDS.TXT cada vez que alguien entra al top 5.
 *   - Sin tarjeta: charts embebidos (sd_manager_songs.c) y récords solo en RAM.
 */
#include "xparameters.h"
#include "xil_printf.h"
#include "hw.h"
#include "ff.h"

static FATFS fs  __attribute__((aligned(32)));

static char fbuf[16384] __attribute__((aligned(32)));    /* buffer de archivo (global) */

static const char *const SONG_PATH[N_SONGS] = { NULL ,"0:/SONG1.SHC", "0:/SONG2.SHC" };
#define RECORDS_PATH "0:/RECORDS.TXT"

/* Monta la tarjeta (f_mount, contenidos del curso). Sin tarjeta queda el respaldo. */
int sd_init(SdCtx *sd)
{
    FRESULT r = f_mount(&fs, "0:/", 1);                  /* opt = 1: monta ahora */
    sd->mounted = (u8)(r == FR_OK);
    xil_printf("[SD] f_mount = %d (%s)\r\n", (int)r,
               sd->mounted ? "tarjeta lista" : "sin tarjeta: respaldo embebido y records en RAM");
    return sd->mounted ? 0 : -1;
}

/* Lee hasta 'max' bytes de 'path' (f_open / f_read, contenidos del curso). */
int sd_read_file(const char *path, char *buf, u32 max, u32 *len)
{
    FIL f;
    UINT br = 0;
    FRESULT r;
    if (f_open(&f, path, FA_READ) != FR_OK) return -1;
    r = f_read(&f, buf, max, &br);
    f_close(&f);
    *len = br;
    return (r == FR_OK) ? 0 : -2;
}

/* Crea o reemplaza 'path' con 'len' bytes (f_open / f_write, contenidos del curso). */
int sd_write_file(const char *path, const char *buf, u32 len)
{
    FIL f;
    UINT bw = 0;
    FRESULT r;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return -1;
    r = f_write(&f, buf, len, &bw);
    f_close(&f);
    return (r == FR_OK && bw == len) ? 0 : -2;
}

/* Carga la canción song_idx: primero desde la SD; si no se puede, desde el respaldo. */
int sd_load_song(SdCtx *sd, Song *song, Note *pool, u8 song_idx)
{
    u32 len = 0;
    int n;
    if (song_idx >= N_SONGS) return -1;

    if (sd->mounted && SONG_PATH[song_idx] != NULL &&
        sd_read_file(SONG_PATH[song_idx], fbuf, sizeof fbuf, &len) == 0) {   /* el Demo no tiene archivo (ruta NULL) */
        n = song_parse(fbuf, len, song, pool, MAX_NOTES);
        xil_printf("[SD] %s: %d notas\r\n", SONG_PATH[song_idx], n);
        if (n > 0) return n;
    }
    if      (song_idx == LEVEL_DEMO) n = song_parse(g_demo_shc,  g_demo_shc_len,  song, pool, MAX_NOTES);
    else if (song_idx == 1u)         n = song_parse(g_song1_shc, g_song1_shc_len, song, pool, MAX_NOTES);
    else                             n = song_parse(g_song2_shc, g_song2_shc_len, song, pool, MAX_NOTES);
    xil_printf("[SD] nivel %d embebido: %d notas\r\n", song_idx, n);
    return n;                                   /* notas (> 0) o SONG_ERR_* (< 0) */
}

int sd_load_records(SdCtx *sd, Record *r, u8 max)
{
    u32 len = 0;
    if (!sd->mounted || sd_read_file(RECORDS_PATH, fbuf, sizeof fbuf, &len) != 0) return 0;
    return records_parse(fbuf, len, r, max);
}

int sd_save_records(SdCtx *sd, const Record *r, u8 n)
{
    u32 len;
    int s;
    if (!sd->mounted) return -1;                         /* sin tarjeta quedan en RAM */
    len = records_format(r, n, fbuf, sizeof fbuf);
    s = sd_write_file(RECORDS_PATH, fbuf, len);
    xil_printf("[SD] RECORDS.TXT guardado: %d\r\n", s);
    return s;
}
