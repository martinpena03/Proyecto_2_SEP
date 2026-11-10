/*
 * test.h - Mini marco de pruebas para la lógica portable de Sep Hero.
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio.
 */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include "stubs.h"

extern int g_fails;     /* comprobaciones fallidas */
extern int g_checks;    /* comprobaciones hechas   */

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_fails++; \
    printf("FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

void test_engine(void);
void test_joy(void);
void test_formats(void);
void test_audio_seq(void);
void test_debcfg(void);
void test_uart_cfg(void);
void test_fsm(void);
void test_render(void);

#endif /* TEST_H */
