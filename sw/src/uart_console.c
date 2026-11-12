/*
 * uart_console.c - Consola del avance por la UART (115200 8N1): la máquina de estados se
 *             representa y se maneja desde el PC, sin pantalla.
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio, reducido de
 *   la consola de diagnóstico del proyecto completo (rama proyecto-final, uart_console.c).
 *
 * Teclas: 1 OK (como S1)   2 atrás/pausa (como S2)   u arriba   j abajo   JOY_SEL (placa) = música ON/OFF
 *         k sacudida virtual   p autoplay (juega solo la canción)
 *         s estado   o sensor de luz   d IP debouncer_axi   t interrupciones
 *         T buzzer (TTC0)   h ayuda
 * En el estado CONFIG la UART la usa uart_config.c (parámetros del IP).
 */
#include "xparameters.h"
#include "xil_printf.h"
#include "xil_io.h"
#include "hw.h"
#include "lib/I2C.h"

static UiEvent g_inj = UI_NONE;    /* evento de interfaz inyectado               */
static u8  g_auto;                 /* 1 = autoplay: encola los golpes perfectos  */
static u16 g_auto_next;
static FsmState g_last_st = ST_INIT;

UiEvent console_pop_event(void)
{
    UiEvent e = g_inj;
    g_inj = UI_NONE;
    return e;
}

static const char *st_name(FsmState s)
{
    static const char *const N[] = { "INIT", "MENU", "LEVEL", "PLAY", "PAUSE", "GAMEOVER", "RECORDS", "CONFIG" };
    return ((unsigned)s < 8u) ? N[s] : "?";
}

static void help(void)
{
    xil_printf("\r\n--- consola del avance Sep Hero ---\r\n"
               " 1 OK (S1)   2 atras/pausa (S2)   u arriba   j abajo   JOY_SEL (placa) musica ON/OFF\r\n"
               " k sacudida virtual   p autoplay on/off (en PLAY)\r\n"
               " s estado   o sensor de luz   d IP Debouncer_IP   t interrupciones   T buzzer\r\n");
}

static void dump_status(GameCtx *ctx)
{
    xil_printf("[ST] %s cursor=%d cancion=%d | song_ms=%d score=%d hits=%d miss=%d rock=%d | auto=%d tema=%s\r\n",
               st_name(ctx->st), ctx->cursor, ctx->song_sel, (int)clock_song_ms(), (int)ctx->gs.score,
               ctx->gs.hits, ctx->gs.misses, ctx->gs.rock, g_auto,
               (ctx->gs.theme == THEME_LIGHT) ? "CLARO" : "OSCURO");
}

static void dump_opt(void)
{
    xil_printf("[OPT] RESULT=0x%04x CONFIG=0x%04x LOW=0x%04x HIGH=0x%04x | lux=%d tema=%s cambios=%d\r\n",
               opt_read_reg(0x00), opt_read_reg(0x01), opt_read_reg(0x02), opt_read_reg(0x03),
               opt3001_read_lux(), (theme_current() == THEME_LIGHT) ? "CLARO" : "OSCURO",
               (int)g_theme_changes);
}

static void dump_deb(GameCtx *ctx)
{
    u32 b = XPAR_DEBOUNCER_IP_BASEADDR;
    xil_printf("[DEB] CTRL=0x%x DEB_MS=%d BTN_MASK=0x%x STATE=0x%x EDGE=0x%x LED_CTRL=0x%02x (cfg %d ms, mask 0x%x, leds 0x%x)\r\n",
               (int)Xil_In32(b + 0x00), (int)Xil_In32(b + 0x04), (int)Xil_In32(b + 0x08),
               (int)Xil_In32(b + 0x0C), (int)Xil_In32(b + 0x10), (int)Xil_In32(b + 0x14),
               ctx->cfg.deb_ms, ctx->cfg.btn_mask, ctx->cfg.led_pattern);
}

static void dump_timers(void)
{
    u16 acc[3], cpg;
    u32 shakes;
    sensors_acc_debug(acc, &cpg, &shakes);
    xil_printf("[IRQ] sys_ms=%d timer0=%d timer1=%d debouncer=%d | timer1_cb max=%d us | acc=(%d,%d,%d) cpg=%d movimientos=%d\r\n",
               (int)clock_sys_ms(), (int)g_irq_cnt[0], (int)g_irq_cnt[1], (int)g_irq_cnt[2],
               (int)g_t1_max_us, acc[0], acc[1], acc[2], cpg, (int)shakes);
    g_t1_max_us = 0;
}

static void dump_ttc(GameCtx *ctx)
{
    u32 itv, mt, ps, cc, clk, div, f = 0;
    audio_get_regs(&itv, &mt, &ps, &cc, &clk);
    /* Prescaler: 16 = deshabilitado (divide por 1); N = divide por 2^(N+1). */
    div = (ps >= 16u) ? 1u : (1u << (ps + 1u));
    if (itv > 0u) f = clk / div / itv;
    xil_printf("[TTC] mudo=%d nota=%d Hz | intervalo=%d match=%d presc=%d -> %d Hz, duty=%d%% | CNT_CTRL=0x%02x (%s, onda %s)\r\n",
               audio_is_muted(), ctx->bz.cur_f, (int)itv, (int)mt, (int)ps, (int)f,
               (int)(itv ? (mt * 100u / itv) : 0u), (int)cc,
               (cc & 0x01u) ? "detenido" : "corriendo", (cc & 0x20u) ? "deshabilitada" : "habilitada");
}

void console_poll(GameCtx *ctx)
{
    int c;
    if (ctx->st == ST_CONFIG) return;             /* la consola la usa uart_config */
    c = uart_hal_getc();
    if (c < 0) return;
    switch (c) {
    case 'h': case '?': help(); break;
    case '1': g_inj = UI_OK; break;
    case '2': g_inj = UI_BACK; break;
    case 'u': g_inj = UI_UP; break;
    case 'j': g_inj = UI_DOWN; break;
    case 'k': g_inj = UI_SHAKE; break;
    case 'p': g_auto = (u8)!g_auto; xil_printf("[AUTO] %d\r\n", g_auto); break;
    case 's': dump_status(ctx); break;
    case 'o': dump_opt(); break;
    case 'd': dump_deb(ctx); break;
    case 't': dump_timers(); break;
    case 'T': dump_ttc(ctx); break;
    default: break;
    }
}

/* Una vez por frame: informa el resultado de la partida y, con autoplay, encola el golpe
 * de cada nota en su instante exacto (no hay joystick en el avance). */
void console_frame(GameCtx *ctx)
{
    if (ctx->st != g_last_st) {
        if (ctx->st == ST_PLAY && clock_song_ms() < 200u) g_auto_next = 0;
        if (ctx->st == ST_GAMEOVER) {
            xil_printf("[GAME] fin: %s score=%d hits=%d miss=%d racha_max=%d rock=%d\r\n",
                       (ctx->result == RESULT_WIN) ? "VICTORIA" : "DERROTA", (int)ctx->gs.score,
                       ctx->gs.hits, ctx->gs.misses, ctx->gs.max_streak, ctx->gs.rock);
        }
        g_last_st = ctx->st;
    }
    if (g_auto && ctx->st == ST_PLAY) {
        u32 now = clock_song_ms();
        while (g_auto_next < ctx->song.n && ctx->song.chart[g_auto_next].t_ms <= now + 20u) {
            hit_push((u8)ctx->song.chart[g_auto_next].lane, ctx->song.chart[g_auto_next].t_ms);
            g_auto_next++;
        }
    }
}
