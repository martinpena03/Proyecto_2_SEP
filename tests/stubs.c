/*
 * stubs.c - Reemplazos en el PC de las funciones de hardware (dueño A).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
 * (docs/MANUAL_IMPLEMENTACION.md §12.1).
 *
 * Permiten compilar y probar la lógica portable (game_engine, game_fsm, render,
 * audio_seq, uart_config, ...) sin la Zybo. Cada stub guarda lo que recibió en variables
 * globales que los tests inspeccionan (ver stubs.h). lcd_fill / lcd_blit están en
 * sim/fb.c (framebuffer).
 */
#include <string.h>
#include "stubs.h"

u32   stub_sys_ms, stub_song_ms;
int   stub_song_running;
int   stub_play_mode;
int   stub_flush_count;
u16   stub_shake_mg;
u8    stub_crowd;
Theme stub_theme;
u16   stub_tone_f;
u32   stub_tone_calls;
int   stub_deb_inits;
DebCfg stub_deb_cfg;
int   stub_sd_mounted;
int   stub_sd_saves;
char  stub_sd_records[256];
char  stub_uart_out[4096];

static Hit  hitq[64];
static int  hq_head, hq_tail;
static char uart_in[512];
static int  ui_len, ui_pos;

void stub_reset(void)
{
    stub_sys_ms = stub_song_ms = 0;
    stub_song_running = 0;
    stub_play_mode = 0;
    stub_flush_count = 0;
    stub_shake_mg = 0;
    stub_crowd = 0;
    stub_theme = THEME_DARK;
    stub_tone_f = 0;
    stub_tone_calls = 0;
    stub_deb_inits = 0;
    memset(&stub_deb_cfg, 0, sizeof stub_deb_cfg);
    stub_sd_mounted = 0;
    stub_sd_saves = 0;
    stub_sd_records[0] = '\0';
    stub_uart_out[0] = '\0';
    hq_head = hq_tail = 0;
    ui_len = ui_pos = 0;
}

void stub_advance(u32 ms)
{
    stub_sys_ms += ms;
    if (stub_song_running) stub_song_ms += ms;
}

void stub_push_hit(u8 lane, u32 song_ms)
{
    if (hq_head < (int)(sizeof hitq / sizeof hitq[0])) {
        hitq[hq_head].lane = lane;
        hitq[hq_head].song_ms = song_ms;
        hq_head++;
    }
}

void stub_uart_feed(const char *s)
{
    size_t n = strlen(s);
    if (ui_pos == ui_len) ui_pos = ui_len = 0;
    if (ui_len + n < sizeof uart_in) {
        memcpy(uart_in + ui_len, s, n);
        ui_len += (int)n;
    }
}

/* ----------------------------- relojes (isr.c) ----------------------------- */
u32  clock_sys_ms(void)        { return stub_sys_ms; }
u32  clock_song_ms(void)       { return stub_song_ms; }
void clock_song_set(u32 ms)    { stub_song_ms = ms; }
void clock_song_run(int run)   { stub_song_running = (run != 0); }

/* ----------------------------- entradas (input.c) -------------------------- */
void input_set_play_mode(int play)      { stub_play_mode = play; }
void input_flush(void)                  { hq_head = hq_tail = 0; stub_flush_count++; }
UiEvent ui_pop(void)                    { return UI_NONE; }
int hit_pop(Hit *out)
{
    if (hq_tail == hq_head) return 0;
    *out = hitq[hq_tail++];
    if (hq_tail == hq_head) hq_head = hq_tail = 0;
    return 1;
}

/* ----------------------------- sensores y tema ----------------------------- */
void  sensors_set_shake_threshold(u16 mg) { stub_shake_mg = mg; }
u8    sensors_crowd_level(void)           { return stub_crowd; }
Theme theme_current(void)                 { return stub_theme; }

/* ----------------------------- buzzer (audio.c) ---------------------------- */
void buzzer_tone(Buzzer_t *bz, u16 f_hz, u8 vol)
{
    (void)vol;
    if (bz) bz->cur_f = f_hz;
    stub_tone_f = f_hz;
    stub_tone_calls++;
}

/* ----------------------------- IP propio (debouncer_axi.c) ----------------- */
int debouncer_init(Debouncer *dev, const DebCfg *cfg)
{
    if (dev == NULL || cfg == NULL) return -1;
    if (debcfg_validate(cfg) != 0) return -2;
    dev->base = 0x43C00000u;
    dev->cfg  = *cfg;
    stub_deb_cfg = *cfg;
    stub_deb_inits++;
    return 0;
}
u8   debouncer_get_edges(Debouncer *dev)       { (void)dev; return 0; }
u8   debouncer_get_state(const Debouncer *dev) { (void)dev; return 0; }
void debouncer_set_theme(Debouncer *dev, int light) { dev->light = (u8)(light != 0); }
u8   debouncer_led_readback(const Debouncer *dev)
{
    return (u8)((dev->light ? 0x10u : 0u) | (dev->cfg.led_pattern & 0xFu));
}
void debouncer_readback(const Debouncer *dev, u16 *deb_ms, u8 *btn_mask)
{
    *deb_ms   = dev->cfg.deb_ms;
    *btn_mask = dev->cfg.btn_mask;
}

/* ----------------------------- UART (uart_hal.c) --------------------------- */
int uart_hal_getc(void)
{
    if (ui_pos >= ui_len) return -1;
    return (unsigned char)uart_in[ui_pos++];
}
void uart_hal_puts(const char *s)
{
    size_t used = strlen(stub_uart_out);
    size_t n = strlen(s);
    if (used + n < sizeof stub_uart_out) memcpy(stub_uart_out + used, s, n + 1);
}

/* ----------------------------- SD (sd_storage.c) --------------------------- */
int sd_init(SdCtx *sd)
{
    sd->mounted = (u8)(stub_sd_mounted != 0);
    return sd->mounted ? 0 : -1;
}
int sd_load_song(SdCtx *sd, Song *song, Note *pool, u8 song_idx)
{
    (void)sd;
    if (song_idx == 0u) return song_parse(g_song1_shc, g_song1_shc_len, song, pool, MAX_NOTES);
    if (song_idx == 1u) return song_parse(g_song2_shc, g_song2_shc_len, song, pool, MAX_NOTES);
    return -1;
}
int sd_load_records(SdCtx *sd, Record *r, u8 max)
{
    (void)sd;
    return records_parse(stub_sd_records, (u32)strlen(stub_sd_records), r, max);
}
int sd_save_records(SdCtx *sd, const Record *r, u8 n)
{
    u32 len;
    (void)sd;
    len = records_format(r, n, stub_sd_records, sizeof stub_sd_records - 1u);
    stub_sd_records[len] = '\0';
    stub_sd_saves++;
    return 0;
}
