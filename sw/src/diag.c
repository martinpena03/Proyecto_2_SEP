/*
 * diag.c - Consola de diagnóstico por UART (solo si se compila con -DSEP_DIAG).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio.
 *
 * Permite verificar la placa sin tocarla (volcar registros y lecturas, inyectar eventos
 * de menú, jugar sola) y ajustar en vivo lo que solo se puede ver con la placa al frente
 * (offsets del LCD, signo del eje Y, sonido). Un comando = una tecla; no se atiende
 * mientras el estado CONFIG usa la consola. 'h' muestra la ayuda.
 *
 * Los comandos de sonido ('m', 'b') son para usar con el usuario presente: el firmware
 * autónomo arranca en mudo (SEP_MUTE) y 'b' no suena mientras el mudo esté activo.
 */
#ifdef SEP_DIAG
#include "xil_printf.h"
#include "xil_io.h"
#include "xparameters.h"
#include "sleep.h"
#include "hw.h"
#include "text.h"
#include "lib/ADC.h"
#include "lib/I2C.h"
#include "lib/LCD_GUI.h"

static u8  g_auto;                 /* 1 = autoplay: inyecta los golpes perfectos */
static u16 g_auto_next;
static u8  g_auto_miss;            /* falla 1 de cada N notas (0 = ninguna)      */
static UiEvent g_inj = UI_NONE;    /* evento de interfaz inyectado               */
static u32 g_fsm_max, g_rnd_max, g_rnd_sum, g_frames;
static FsmState g_last_st = ST_INIT;

UiEvent diag_pop_event(void)
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
    xil_printf("\r\n--- diagnostico Sep Hero ---\r\n"
               " s estado   a ADC   o OPT3001   d debouncer   t timers/IRQ   T TTC0 (buzzer)\r\n"
               " 1 OK(S1)   2 BACK(S2)   u arriba   j abajo   k sacudida   (eventos inyectados)\r\n"
               " p autoplay on/off   P autoplay fallando 1 de 3\r\n"
               " l patron LCD   x/X y/Y offsets LCD -/+   i invertir eje Y\r\n"
               " f buscador pin OPT_INT   g buscador pin DRDY del TMP006   F forzar interrupcion OPT (mueve la ventana)\r\n"
               " m mudo on/off   b barrido del buzzer (solo con el usuario presente)\r\n");
}

static void dump_adc(void)
{
    static const u8 ch[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    u16 v[8], x, y, x0, y0, acc[3], cpg, p2p, noise;
    u32 shakes;
    adc_read_chain(ch, 8, v);
    xil_printf("[ADC] cadena: joyX=%d joyY=%d acX=%d acY=%d acZ=%d mic=%d pot1=%d pot2=%d\r\n",
               v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    xil_printf("[ADC] curso : joyX=%d joyY=%d acX=%d acY=%d acZ=%d mic=%d\r\n",
               read_joyx(), read_joyy(), read_acx(), read_acy(), read_acz(), read_MIC());
    input_get_joy(&x, &y, &x0, &y0);
    sensors_get_debug(acc, &cpg, &p2p, &noise, &shakes);
    xil_printf("[JOY] x=%d y=%d centro=(%d,%d)\r\n", x, y, x0, y0);
    xil_printf("[SENS] cpg=%d mic_p2p=%d mic_ruido=%d publico=%d sacudidas=%d\r\n",
               cpg, p2p, noise, sensors_crowd_level(), (int)shakes);
}

static void dump_opt(void)
{
    xil_printf("[OPT] RESULT=0x%04x CONFIG=0x%04x LOW=0x%04x HIGH=0x%04x ID=0x%04x/0x%04x\r\n",
               opt_read_reg(0x00), opt_read_reg(0x01), opt_read_reg(0x02), opt_read_reg(0x03),
               opt_read_reg(0x7E), opt_read_reg(0x7F));
    xil_printf("[OPT] lux=%d tema=%s gpio1=0x%03x bit=%d cambios=%d\r\n", opt3001_read_lux(),
               (theme_current() == THEME_LIGHT) ? "CLARO" : "OSCURO",
               (int)gpio1_read(), g_opt_bit, (int)g_theme_changes);
}

static void dump_deb(GameCtx *ctx)
{
    u32 b = XPAR_DEBOUNCER_AXI_0_BASEADDR;
    xil_printf("[DEB] CTRL=0x%x DEB_MS=%d BTN_MASK=0x%x STATE=0x%x EDGE=0x%x (cfg %d ms, mask 0x%x)\r\n",
               (int)Xil_In32(b + 0x00), (int)Xil_In32(b + 0x04), (int)Xil_In32(b + 0x08),
               (int)Xil_In32(b + 0x0C), (int)Xil_In32(b + 0x10), ctx->cfg.deb_ms, ctx->cfg.btn_mask);
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

static void dump_status(GameCtx *ctx)
{
    xil_printf("[ST] %s cursor=%d cancion=%d sd=%d | song_ms=%d score=%d hits=%d miss=%d racha=%d rock=%d star=%d/%d | auto=%d\r\n",
               st_name(ctx->st), ctx->cursor, ctx->song_sel, ctx->sd.mounted, (int)clock_song_ms(),
               (int)ctx->gs.score, ctx->gs.hits, ctx->gs.misses, ctx->gs.streak, ctx->gs.rock,
               ctx->gs.star_phase, ctx->gs.star_bar, g_auto);
}

static void dump_timers(void)
{
    xil_printf("[IRQ] sys_ms=%d t0=%d t1=%d deb=%d gpio=%d | timer1_cb max=%d us | fsm max=%d us render max=%d us prom=%d us (%d frames)\r\n",
               (int)clock_sys_ms(), (int)g_irq_cnt[0], (int)g_irq_cnt[1], (int)g_irq_cnt[2], (int)g_irq_cnt[3],
               (int)g_t1_max_us, (int)g_fsm_max, (int)g_rnd_max,
               (int)(g_frames ? g_rnd_sum / g_frames : 0u), (int)g_frames);
    g_fsm_max = g_rnd_max = g_rnd_sum = g_frames = 0;
    g_t1_max_us = 0;
}

/* Patrón de prueba: marco de 1 px en 0 y 127, barras R/G/B y rótulos de orientación.
 * El título se dibuja con GUI_DisString_EN del driver del curso (referencia). */
static void lcd_pattern(GameCtx *ctx)
{
    u8 xo, yo;
    lcd_fill(0, 0, 128, 128, 0x0000);
    lcd_fill(0, 0, 128, 1, 0xFFFF);   lcd_fill(0, 127, 128, 128, 0xFFFF);
    lcd_fill(0, 0, 1, 128, 0xFFFF);   lcd_fill(127, 0, 128, 128, 0xFFFF);
    lcd_fill(8, 40, 44, 70, 0xF800);  lcd_fill(46, 40, 82, 70, 0x07E0);  lcd_fill(84, 40, 120, 70, 0x001F);
    (void)text_draw(20, 74, "R", &Font12, 0xFFFF, 0);
    (void)text_draw(58, 74, "G", &Font12, 0xFFFF, 0);
    (void)text_draw(96, 74, "B", &Font12, 0xFFFF, 0);
    text_center(4, "ARRIBA", &Font12, 0xFFFF, 0);
    text_center(110, "ABAJO", &Font12, 0xFFFF, 0);
    (void)text_draw(3, 56, "I", &Font8, 0xFFFF, 0);
    (void)text_draw(120, 56, "D", &Font8, 0xFFFF, 0);
    GUI_DisString_EN(30, 22, "curso", &Font12, BLACK, YELLOW);
    lcd_get_offsets(&xo, &yo);
    xil_printf("[LCD] patron dibujado; offsets x=%d y=%d\r\n", xo, yo);
    ctx->redraw_seq++;                       /* el siguiente cambio de estado repinta */
}

/* Nombres de los pines candidatos, en el orden de los bits de axi_gpio_1 del build
 * "finder" (hw/scripts/02_build_bd.tcl). */
static const char *const CAND[25] = {
    "U14(JD7)", "U15(JD8)", "V15(JC1)", "W15(JC2)", "T11(JC3)", "T10(JC4)", "U17(JE8)", "T17(JE9)", "Y17(JE10)",
    "N15(JA1)", "L14(JA2)", "K16(JA3)", "K14(JA4)", "N16(JA7)", "L15(JA8)", "J16(JA9)", "J14(JA10)",
    "V8(JB1)", "W8(JB2)", "U7(JB3)", "V7(JB4)", "Y7(JB7)", "Y6(JB8)", "V6(JB9)", "W6(JB10)" };

static void print_bits(const char *tag, u32 m)
{
    int i;
    xil_printf("[FIND] %s = 0x%07x:", tag, (int)m);
    for (i = 0; i < 25; i++) if (m & (1u << i)) xil_printf(" %s", CAND[i]);
    xil_printf("\r\n");
}

/* Buscador del pin INT del OPT3001 (manual §15.1): invierte el bit POL de CONFIG (el pin
 * cambia de nivel al instante) y mira cuál de las entradas de axi_gpio_1 lo sigue. */
static void opt_finder(void)
{
    u32 a[6], follow = 0x1FFFFFFu, anti = 0x1FFFFFFu;
    int i;
    for (i = 0; i < 6; i++) {
        (void)opt_write_reg(0x01, (i & 1) ? 0xC418u : 0xC410u);     /* POL = i & 1 */
        usleep(300000);
        a[i] = gpio1_read() & 0x1FFFFFFu;
        xil_printf("[FIND] POL=%d CONFIG=0x%04x gpio1=0x%07x\r\n", i & 1, opt_read_reg(0x01), (int)a[i]);
    }
    (void)opt_write_reg(0x01, 0xC410u);
    for (i = 0; i < 6; i++) {
        /* bits que valen POL en todas las muestras / que valen ~POL en todas. */
        follow &= (i & 1) ? a[i] : ~a[i];
        anti   &= (i & 1) ? ~a[i] : a[i];
    }
    print_bits("siguen a POL", follow);
    print_bits("inversos a POL", anti);
}

/* Igual para el pin DRDY del sensor de temperatura TMP006 (0x40), la alternativa que
 * permite AO2: con DRDY_EN = 1 el pin baja al terminar cada conversion y sube al leer. */
static void tmp_finder(void)
{
    u8 w[3] = { 0x02, 0x75, 0x00 };          /* CONFIG: conversion continua, 1 conv/s, DRDY_EN */
    u8 r[2] = { 0, 0 };
    u32 low = 0x1FFFFFFu, high = 0x1FFFFFFu, g;
    int i;
    XIic_Send(iic.BaseAddress, 0x40, w, 3, XIIC_STOP);
    w[0] = 0xFE;                              /* ID del fabricante: 0x5449 */
    XIic_Send(iic.BaseAddress, 0x40, w, 1, XIIC_REPEATED_START);
    XIic_Recv(iic.BaseAddress, 0x40, r, 2, XIIC_STOP);
    xil_printf("[FIND] TMP006 ID=0x%02x%02x\r\n", r[0], r[1]);
    for (i = 0; i < 4; i++) {
        usleep(1300000);                      /* termina una conversion: DRDY en bajo */
        g = gpio1_read() & 0x1FFFFFFu;
        low &= ~g;
        w[0] = 0x00;                          /* leer el voltaje del sensor libera DRDY */
        XIic_Send(iic.BaseAddress, 0x40, w, 1, XIIC_REPEATED_START);
        XIic_Recv(iic.BaseAddress, 0x40, r, 2, XIIC_STOP);
        usleep(20000);
        high &= gpio1_read() & 0x1FFFFFFu;
        xil_printf("[FIND] TMP ciclo %d: listo=0x%07x leido=0x%07x\r\n", i, (int)g, (int)(gpio1_read() & 0x1FFFFFFu));
    }
    print_bits("DRDY del TMP006 (bajo al terminar, alto al leer)", low & high);
}

/* Fuerza la interrupción del OPT3001 sin tocar el sensor: mueve la ventana para que la
 * luz actual quede fuera. theme_task() cambia el tema y rearma la ventana normal. */
static void opt_force(void)
{
    if (theme_current() == THEME_LIGHT) (void)opt_write_reg(0x02, 0xBFFFu);   /* LOW al máximo  */
    else                                (void)opt_write_reg(0x03, 0x0000u);   /* HIGH al mínimo */
    xil_printf("[OPT] ventana forzada; esperando la interrupcion (gpio irq=%d)\r\n", (int)g_irq_cnt[3]);
}

static void buzzer_sweep(GameCtx *ctx)
{
    u16 f;
    if (audio_is_muted()) {
        xil_printf("[BUZ] en mudo: el barrido no suena. Usa 'm' (solo con el usuario presente).\r\n");
        return;
    }
    buzzer_stop(&ctx->bz);
    for (f = 500; f <= 5000; f = (u16)(f + 250)) {
        buzzer_tone(&ctx->bz, f, ctx->bz.vol);
        xil_printf("[BUZ] %d Hz\r\n", f);
        usleep(250000);
    }
    buzzer_tone(&ctx->bz, 0, 0);
}

/* Atiende a lo más un comando por vuelta del bucle principal. */
void diag_poll(GameCtx *ctx)
{
    int c;
    u8 xo, yo;
    if (ctx->st == ST_CONFIG) return;             /* la consola la usa uart_config */
    c = uart_hal_getc();
    if (c < 0) return;
    lcd_get_offsets(&xo, &yo);
    switch (c) {
    case 'h': case '?': help(); break;
    case 's': dump_status(ctx); break;
    case 'a': dump_adc(); break;
    case 'o': dump_opt(); break;
    case 'd': dump_deb(ctx); break;
    case 't': dump_timers(); break;
    case 'T': dump_ttc(ctx); break;
    case '1': g_inj = UI_OK; break;
    case '2': g_inj = UI_BACK; break;
    case 'u': g_inj = UI_UP; break;
    case 'j': g_inj = UI_DOWN; break;
    case 'k': g_inj = UI_SHAKE; break;
    case 'p': g_auto = (u8)!g_auto; g_auto_miss = 0; xil_printf("[AUTO] %d\r\n", g_auto); break;
    case 'P': g_auto = 1; g_auto_miss = 3; xil_printf("[AUTO] fallando 1 de 3\r\n"); break;
    case 'l': lcd_pattern(ctx); break;
    case 'x': lcd_set_offsets((u8)(xo - 1u), yo); lcd_pattern(ctx); break;
    case 'X': lcd_set_offsets((u8)(xo + 1u), yo); lcd_pattern(ctx); break;
    case 'y': lcd_set_offsets(xo, (u8)(yo - 1u)); lcd_pattern(ctx); break;
    case 'Y': lcd_set_offsets(xo, (u8)(yo + 1u)); lcd_pattern(ctx); break;
    case 'i': { static u8 inv = JOY_INVERT_Y; inv = (u8)!inv; input_set_joy_inv_y(inv);
                xil_printf("[JOY] invertir Y = %d\r\n", inv); break; }
    case 'f': opt_finder(); break;
    case 'g': tmp_finder(); break;
    case 'F': opt_force(); break;
    case 'm': audio_set_mute(!audio_is_muted()); xil_printf("[BUZ] mudo = %d\r\n", audio_is_muted()); break;
    case 'b': buzzer_sweep(ctx); break;
    default: break;
    }
}

/* Una vez por frame: estadísticas de tiempo, registro de cambios de estado y autoplay. */
void diag_frame(GameCtx *ctx, u32 fsm_us, u32 render_us)
{
    if (fsm_us > g_fsm_max) g_fsm_max = fsm_us;
    if (render_us > g_rnd_max) g_rnd_max = render_us;
    g_rnd_sum += render_us;
    g_frames++;

    if (ctx->st != g_last_st) {
        xil_printf("[FSM] %s -> %s (t=%d ms)\r\n", st_name(g_last_st), st_name(ctx->st), (int)clock_sys_ms());
        if (ctx->st == ST_PLAY && clock_song_ms() < 200u) g_auto_next = 0;
        if (ctx->st == ST_GAMEOVER) {
            xil_printf("[GAME] fin: %s score=%d hits=%d miss=%d racha_max=%d rock=%d\r\n",
                       (ctx->result == RESULT_WIN) ? "VICTORIA" : "DERROTA", (int)ctx->gs.score,
                       ctx->gs.hits, ctx->gs.misses, ctx->gs.max_streak, ctx->gs.rock);
        }
        g_last_st = ctx->st;
    }

    /* Autoplay: encola el golpe de cada nota en su instante exacto (como si el jugador
     * no fallara), para recorrer una partida completa sin manos. */
    if (g_auto && ctx->st == ST_PLAY) {
        u32 now = clock_song_ms();
        while (g_auto_next < ctx->song.n && ctx->song.chart[g_auto_next].t_ms <= now + 20u) {
            if (g_auto_miss == 0u || (g_auto_next % g_auto_miss) != 0u) {
                hit_push((u8)ctx->song.chart[g_auto_next].lane, ctx->song.chart[g_auto_next].t_ms);
            }
            g_auto_next++;
        }
    }
}
#else
typedef int diag_unused_t;     /* unidad de traducción no vacía cuando no hay diagnóstico */
#endif /* SEP_DIAG */
