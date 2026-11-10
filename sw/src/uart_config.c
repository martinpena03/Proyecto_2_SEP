/*
 * uart_config.c - Configuración por la consola UART del PC (UART_config, AO3).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: basado en la consola de la AYU09 (helloworld.c: menú por UART con lectura de
 *   enteros), reescrito como lector de línea NO bloqueante para que la máquina de
 *   estados siga corriendo y S2 pueda cancelar (docs/MANUAL_IMPLEMENTACION.md §8.7).
 *
 * Portable: solo usa uart_hal_getc() / uart_hal_puts() (uart_hal.c en la placa, stubs
 * en el PC). No usa printf ni scanf.
 *
 * El usuario entrega 5 parámetros: DEB_MS, BTN_MASK y LED_PATTERN (van al IP propio por
 * debouncer_init), JOY_UMBRAL (input.c) y UMBRAL_SHAKE (sensors.c).
 */
#include "interfaces.h"

#define N_STEPS 5u

/* ----------------------- utilidades de formato (sin printf) ----------------------- */

char *fmt_str(char *dst, const char *s)
{
    while (*s) *dst++ = *s++;
    *dst = '\0';
    return dst;
}

char *fmt_u32(char *dst, u32 v)
{
    char tmp[10];
    u8 n = 0;
    do { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v != 0u);
    while (n > 0u) *dst++ = tmp[--n];
    *dst = '\0';
    return dst;
}

char *fmt_hex(char *dst, u32 v, u8 digits)
{
    static const char hx[] = "0123456789ABCDEF";
    while (digits > 0u) {
        digits--;
        *dst++ = hx[(v >> (4u * digits)) & 0xFu];
    }
    *dst = '\0';
    return dst;
}

/* ------------------------------ pasos del diálogo --------------------------------- */

/* Imprime la pregunta del paso actual con el valor vigente. */
static void prompt(const UartCfg *u)
{
    char b[80];
    char *p = b;
    switch (u->step) {
    case 0:
        p = fmt_str(p, "DEB_MS [1-255] (actual ");
        p = fmt_u32(p, u->tmp.deb_ms);
        break;
    case 1:
        p = fmt_str(p, "BTN_MASK [hex 1-7: bit0 S1, bit1 S2, bit2 SEL] (actual ");
        p = fmt_hex(p, u->tmp.btn_mask, 1);
        break;
    case 2:
        p = fmt_str(p, "JOY_UMBRAL [100-450] (actual ");
        p = fmt_u32(p, u->tmp.joy_umbral);
        break;
    case 3:
        p = fmt_str(p, "UMBRAL_SHAKE [mg 200-2000] (actual ");
        p = fmt_u32(p, u->tmp.umbral_shake);
        break;
    default:
        p = fmt_str(p, "LED_PATTERN modo claro [hex 1-F: LD3..LD0] (actual ");
        p = fmt_hex(p, u->tmp.led_pattern, 1);
        break;
    }
    p = fmt_str(p, "): ");
    uart_hal_puts(b);
}

/* Convierte la línea a entero (decimal, o hexadecimal si hex = 1; acepta "0x").
 * Devuelve 0 si es válida. */
static int parse_num(const char *s, int hex, u32 *out)
{
    u32 v = 0;
    u8 n = 0;
    if (hex && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (; *s != '\0'; s++) {
        u32 d;
        if (*s >= '0' && *s <= '9')                  d = (u32)(*s - '0');
        else if (hex && *s >= 'a' && *s <= 'f')      d = (u32)(*s - 'a') + 10u;
        else if (hex && *s >= 'A' && *s <= 'F')      d = (u32)(*s - 'A') + 10u;
        else return -1;
        v = v * (hex ? 16u : 10u) + d;
        if (++n > 6u) return -1;                     /* evita desbordes */
    }
    if (n == 0u) return -1;
    *out = v;
    return 0;
}

/* Procesa la línea terminada. Devuelve 1 si el paso quedó aceptado. */
static int accept_line(UartCfg *u)
{
    u32 v = 0;
    u->line[u->len] = '\0';
    if (u->len == 0u) return 1;                       /* línea vacía: conserva el valor */
    if (parse_num(u->line, u->step == 1u || u->step == 4u, &v) != 0) return 0;
    switch (u->step) {
    case 0:
        if (v < DEB_MS_MIN || v > DEB_MS_MAX) return 0;
        u->tmp.deb_ms = (u16)v;
        break;
    case 1:
        if (v < 1u || v > 7u || (v & 0x5u) == 0u) return 0;   /* hace falta S1 o SEL */
        u->tmp.btn_mask = (u8)v;
        break;
    case 2:
        if (v < JOY_UMBRAL_MIN || v > JOY_UMBRAL_MAX) return 0;
        u->tmp.joy_umbral = (u16)v;
        break;
    case 3:
        if (v < SHAKE_MG_MIN || v > SHAKE_MG_MAX) return 0;
        u->tmp.umbral_shake = (u16)v;
        break;
    default:
        if (v < LED_PAT_MIN || v > LED_PAT_MAX) return 0;
        u->tmp.led_pattern = (u8)v;
        break;
    }
    return 1;
}

/* Inicia el diálogo a partir de la configuración vigente. */
void uart_cfg_begin(UartCfg *u, const DebCfg *current)
{
    u->step = 0;
    u->len  = 0;
    u->last_eol = 0;
    u->tmp  = *current;
    uart_hal_puts("\r\n=== SEP HERO :: CONFIGURACION (S2 en la placa cancela) ===\r\n");
    uart_hal_puts("Enter sin escribir nada conserva el valor actual.\r\n");
    prompt(u);
}

/* Consume los caracteres disponibles sin bloquear.
 * Devuelve 0 mientras el diálogo sigue y 1 cuando *out tiene una configuración validada. */
int uart_cfg_poll(UartCfg *u, DebCfg *out)
{
    int c;
    while ((c = uart_hal_getc()) >= 0) {
        if (c == '\r' || c == '\n') {
            if (c == '\n' && u->len == 0u && u->last_eol == '\r') {
                u->last_eol = 0;                      /* '\n' de un par CR LF: se ignora */
                continue;
            }
            u->last_eol = (u8)c;                      /* recuerda el último terminador */
            uart_hal_puts("\r\n");
            if (accept_line(u)) {
                u->step++;
            } else {
                uart_hal_puts("Valor fuera de rango\r\n");
            }
            u->len = 0;
            if (u->step >= N_STEPS) {
                char b[128];
                char *p = b;
                if (debcfg_validate(&u->tmp) != 0) {  /* no debería ocurrir: se revalida */
                    uart_hal_puts("Configuracion invalida, se repite\r\n");
                    u->step = 0;
                    prompt(u);
                    continue;
                }
                p = fmt_str(p, "OK -> DEB_MS=");       p = fmt_u32(p, u->tmp.deb_ms);
                p = fmt_str(p, " BTN_MASK=0x");        p = fmt_hex(p, u->tmp.btn_mask, 1);
                p = fmt_str(p, " JOY_UMBRAL=");        p = fmt_u32(p, u->tmp.joy_umbral);
                p = fmt_str(p, " UMBRAL_SHAKE=");      p = fmt_u32(p, u->tmp.umbral_shake);
                p = fmt_str(p, " mg LED_PATTERN=0x");  p = fmt_hex(p, u->tmp.led_pattern, 1);
                p = fmt_str(p, "\r\n");
                uart_hal_puts(b);
                *out = u->tmp;
                return 1;
            }
            prompt(u);
        } else if (c == 0x08 || c == 0x7F) {          /* retroceso */
            u->last_eol = 0;
            if (u->len > 0u) {
                u->len--;
                uart_hal_puts("\b \b");
            }
        } else if (c >= 0x20 && c < 0x7F) {           /* carácter imprimible: eco */
            u->last_eol = 0;
            if (u->len < sizeof(u->line) - 1u) {
                char e[2];
                e[0] = (char)c;
                e[1] = '\0';
                u->line[u->len++] = (char)c;
                uart_hal_puts(e);
            }
        }
    }
    return 0;
}
