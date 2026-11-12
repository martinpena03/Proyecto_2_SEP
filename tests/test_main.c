/*
 * test_main.c - Punto de entrada de los tests en el PC (make -C tests run).
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio.
 * Devuelve != 0 si alguna comprobación falla.
 */
#include "test.h"

int g_fails = 0;
int g_checks = 0;

int main(void)
{
    static const struct { const char *name; void (*fn)(void); } T[] = {
        { "game_manager", test_engine },   { "sd_manager", test_formats },
        { "sound_manager", test_audio_seq },  { "debcfg", test_debcfg },
        { "uart_config", test_uart_cfg }, { "state_machine", test_fsm },
    };
    unsigned i;
    for (i = 0; i < sizeof T / sizeof T[0]; i++) {
        int before = g_fails;
        stub_reset();
        T[i].fn();
        printf("%-22s %s\n", T[i].name, (g_fails == before) ? "ok" : "FALLA");
    }
    printf("%d comprobaciones, %d fallas\n", g_checks, g_fails);
    printf(g_fails ? "TESTS_FAIL\n" : "TESTS_OK\n");
    return g_fails != 0;
}
