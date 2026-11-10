/*
 * sd_storage.c - microSD con FatFs (xilffs) y respaldo embebido (SD_Manager, AC7).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: uso de xilffs visto en la clase del 02-11-2026 ("memoria microSD y FAT en
 *   Vitis": SD0 del PS, f_mount / f_open / f_read / f_write, nombres 8.3, ruta "0:/");
 *   implementación propia (docs/MANUAL_IMPLEMENTACION.md §10).
 *
 * Comportamiento:
 *   - Con tarjeta FAT32: lee 0:/SONGn.SHC (si falta, usa el chart embebido) y lee /
 *     escribe 0:/RECORDS.TXT cada vez que alguien entra al top 5.
 *   - Sin tarjeta: charts embebidos (song_embedded.c) y récords solo en RAM.
 * Con -DSEP_NO_SD no se intenta montar (por si el montaje sin tarjeta se colgara).
 */
#include "xparameters.h"
#include "xil_printf.h"
#include "hw.h"
#ifndef SEP_NO_SD
#include "ff.h"
static FATFS fs;
#endif

static char fbuf[16384] __attribute__((aligned(32)));    /* buffer de archivo (global) */

static const char *const SONG_PATH[N_SONGS] = { "0:/SONG1.SHC", "0:/SONG2.SHC" };
#define RECORDS_PATH "0:/RECORDS.TXT"

int sd_init(SdCtx *sd)
{
#ifndef SEP_NO_SD
    FRESULT r = f_mount(&fs, "0:/", 1);                  /* opt = 1: monta ahora */
    sd->mounted = (u8)(r == FR_OK);
    xil_printf("[SD] f_mount = %d (%s)\r\n", (int)r,
               sd->mounted ? "tarjeta lista" : "sin tarjeta: respaldo embebido y records en RAM");
#else
    sd->mounted = 0;
    xil_printf("[SD] deshabilitada (SEP_NO_SD)\r\n");
#endif
    return sd->mounted ? 0 : -1;
}

int sd_read_file(const char *path, char *buf, u32 max, u32 *len)
{
#ifndef SEP_NO_SD
    FIL f;
    UINT br = 0;
    FRESULT r;
    if (f_open(&f, path, FA_READ) != FR_OK) return -1;
    r = f_read(&f, buf, max, &br);
    f_close(&f);
    *len = br;
    return (r == FR_OK) ? 0 : -2;
#else
    (void)path; (void)buf; (void)max; *len = 0;
    return -1;
#endif
}

int sd_write_file(const char *path, const char *buf, u32 len)
{
#ifndef SEP_NO_SD
    FIL f;
    UINT bw = 0;
    FRESULT r;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return -1;
    r = f_write(&f, buf, len, &bw);
    f_close(&f);
    return (r == FR_OK && bw == len) ? 0 : -2;
#else
    (void)path; (void)buf; (void)len;
    return -1;
#endif
}

/* Carga la canción song_idx: primero desde la SD; si no se puede, desde el respaldo. */
int sd_load_song(SdCtx *sd, Song *song, Note *pool, u8 song_idx)
{
    u32 len = 0;
    int n;
    if (song_idx >= N_SONGS) return -1;
    if (sd->mounted && sd_read_file(SONG_PATH[song_idx], fbuf, sizeof fbuf, &len) == 0) {
        n = song_parse(fbuf, len, song, pool, MAX_NOTES);
        xil_printf("[SD] %s: %d notas\r\n", SONG_PATH[song_idx], n);
        if (n > 0) return n;                             /* chart de la tarjeta válido */
    }
    if (song_idx == 0u) n = song_parse(g_song1_shc, g_song1_shc_len, song, pool, MAX_NOTES);
    else                n = song_parse(g_song2_shc, g_song2_shc_len, song, pool, MAX_NOTES);
    xil_printf("[SD] cancion %d embebida: %d notas\r\n", song_idx + 1, n);
    return n;
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
