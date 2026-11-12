/*
 * sd_manager_formats.c - Formatos de archivo de la microSD (SD_Manager, AC7).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §10.3).
 *
 * Portable y sin heap ni sscanf: se prueba en el PC (tests/test_formats.c).
 * La lectura y escritura física de la tarjeta está en sd_manager.c (xilffs).
 *
 * SONGn.SHC (texto ASCII; '#' inicia un comentario):
 *     NAME=Oda a la Alegria
 *     BPM=120
 *     # t_ms lane(0=IZQ 1=ARRIBA 2=ABAJO 3=DER) freq_hz dur_ms
 *     3000 0 1319 450
 *
 * RECORDS.TXT: hasta 5 líneas "NNN puntaje" con NNN en [A-Z0-9]{3}.
 */
#include "interfaces.h"

/* Códigos de error de song_parse. */
#define SONG_ERR_LINE   (-1)   /* línea que no es NAME=, BPM= ni 4 enteros */
#define SONG_ERR_ORDER  (-2)   /* t_ms decreciente                          */
#define SONG_ERR_FULL   (-3)   /* más notas que el pool                     */
#define SONG_ERR_LANE   (-4)   /* carril fuera de 0..3                      */
#define SONG_ERR_EMPTY  (-5)   /* sin notas                                 */
#define SONG_ERR_DUR    (-6)   /* duración 0                                */

static int is_space(char c) { return c == ' ' || c == '\t'; }
static int is_eol(char c)   { return c == '\n' || c == '\r'; }

/* Lee un entero decimal sin signo desde *pp (hasta 'end'). Avanza *pp.
 * Devuelve 0 si leyó al menos un dígito. */
static int read_u32(const char **pp, const char *end, u32 *out)
{
    const char *p = *pp;
    u32 v = 0;
    int n = 0;
    while (p < end && is_space(*p)) p++;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10u + (u32)(*p - '0');
        p++;
        if (++n > 9) return -1;          /* más de 9 dígitos: desborde */
    }
    *pp = p;
    *out = v;
    return (n > 0) ? 0 : -1;
}

static int starts_with(const char *p, const char *end, const char *key)
{
    while (*key) {
        if (p >= end || *p != *key) return 0;
        p++;
        key++;
    }
    return 1;
}

/* Interpreta un chart. Llena song (nombre, bpm, n, chart = pool) y las notas en pool.
 * Devuelve la cantidad de notas (> 0) o un código SONG_ERR_* (< 0). */
int song_parse(const char *buf, u32 len, Song *song, Note *pool, u16 max)
{
    const char *p = buf;
    const char *end = buf + len;
    u16 n = 0;
    u32 last_t = 0;
    u8 i;

    if (buf == NULL || song == NULL || pool == NULL) return SONG_ERR_LINE;
    for (i = 0; i < sizeof(song->name); i++) song->name[i] = '\0';
    song->name[0] = '?';
    song->bpm   = 120;
    song->n     = 0;
    song->chart = pool;

    while (p < end) {
        const char *eol = p;
        const char *q;
        while (eol < end && !is_eol(*eol)) eol++;      /* [p, eol) = una línea */
        q = p;
        while (q < eol && is_space(*q)) q++;

        if (q == eol || *q == '#') {
            /* línea vacía o comentario */
        } else if (starts_with(q, eol, "NAME=")) {
            const char *s = q + 5;
            const char *e = eol;
            u8 k = 0;
            while (e > s && is_space(e[-1])) e--;      /* sin espacios al final */
            while (s < e && k < sizeof(song->name) - 1u) song->name[k++] = *s++;
            song->name[k] = '\0';
        } else if (starts_with(q, eol, "BPM=")) {
            u32 v;
            q += 4;
            if (read_u32(&q, eol, &v) != 0 || v == 0u || v > 999u) return SONG_ERR_LINE;
            song->bpm = (u16)v;
        } else {
            u32 t, lane, f, d;
            if (read_u32(&q, eol, &t) != 0 || read_u32(&q, eol, &lane) != 0 ||
                read_u32(&q, eol, &f) != 0 || read_u32(&q, eol, &d) != 0) return SONG_ERR_LINE;
            while (q < eol && is_space(*q)) q++;
            if (q < eol && *q != '#') return SONG_ERR_LINE;   /* basura al final */
            if (lane >= N_LANES)       return SONG_ERR_LANE;
            if (d == 0u || d > 0xFFFFu || f > 0xFFFFu) return SONG_ERR_DUR;
            if (t < last_t)            return SONG_ERR_ORDER;
            if (n >= max)              return SONG_ERR_FULL;
            pool[n].t_ms = t;
            pool[n].lane = (Lane)lane;
            pool[n].freq = (u16)f;
            pool[n].dur  = (u16)d;
            last_t = t;
            n++;
        }
        p = eol;
        while (p < end && is_eol(*p)) p++;             /* salta \n, \r\n y líneas vacías */
    }
    if (n == 0u) return SONG_ERR_EMPTY;
    song->n = n;
    return (int)n;
}

/* Inserta (name, score) en la tabla ordenada de mayor a menor. Devuelve la posición
 * en que quedó, o -1 si no entra al top (tabla llena y puntaje no supera al último). */
int records_insert(Record *r, u8 *n, const char *name, u32 score)
{
    u8 pos, i;
    if (r == NULL || n == NULL || name == NULL) return -1;
    pos = 0;
    while (pos < *n && r[pos].score >= score) pos++;   /* empate: queda después */
    if (pos >= MAX_RECORDS) return -1;
    if (*n < MAX_RECORDS) (*n)++;
    for (i = (u8)(*n - 1u); i > pos; i--) r[i] = r[i - 1u];   /* desplaza hacia abajo */
    for (i = 0; i < 3u; i++) r[pos].name[i] = name[i];
    r[pos].name[3] = '\0';
    r[pos].score = score;
    return (int)pos;
}

static int is_name_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* Interpreta RECORDS.TXT. Ignora las líneas inválidas y deja la tabla ordenada.
 * Devuelve la cantidad de récords (0..max). */
int records_parse(const char *buf, u32 len, Record *r, u8 max)
{
    const char *p = buf;
    const char *end = buf + len;
    u8 n = 0;

    if (buf == NULL || r == NULL) return 0;
    if (max > MAX_RECORDS) max = (u8)MAX_RECORDS;
    while (p < end) {
        const char *eol = p;
        const char *q;
        while (eol < end && !is_eol(*eol)) eol++;
        q = p;
        while (q < eol && is_space(*q)) q++;
        if (eol - q >= 5 && is_name_char(q[0]) && is_name_char(q[1]) &&
            is_name_char(q[2]) && is_space(q[3])) {
            const char *s = q + 3;
            u32 sc;
            if (read_u32(&s, eol, &sc) == 0) {
                while (s < eol && is_space(*s)) s++;
                if (s == eol) (void)records_insert(r, &n, q, sc);
            }
        }
        p = eol;
        while (p < end && is_eol(*p)) p++;
    }
    (void)max;
    return (int)n;
}

/* Serializa la tabla. Devuelve la cantidad de bytes escritos (sin '\0' final). */
u32 records_format(const Record *r, u8 n, char *buf, u32 max)
{
    char line[24];
    u32 used = 0;
    u8 i;
    const char *hdr = "# SEP HERO records v1\n";

    if (r == NULL || buf == NULL) return 0;
    while (*hdr && used < max) buf[used++] = *hdr++;
    for (i = 0; i < n; i++) {
        char *p = line;
        const char *s;
        *p++ = r[i].name[0];
        *p++ = r[i].name[1];
        *p++ = r[i].name[2];
        *p++ = ' ';
        p = fmt_u32(p, r[i].score);
        *p++ = '\n';
        *p = '\0';
        for (s = line; *s && used < max; s++) buf[used++] = *s;
    }
    return used;
}
