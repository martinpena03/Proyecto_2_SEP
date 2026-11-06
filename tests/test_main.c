/*
 * test_main.c - Punto de entrada de los tests en el PC.
 * B agrega aquí (o en otros test_*.c) los casos de game_engine y game_fsm.
 */
#include <stdio.h>
#include "game_types.h"

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FALLA %s:%d  %s\n", \
                         __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void)
{
    CHECK(N_LANES == 4);
    CHECK(sizeof(Note) >= 9);
    /* TODO(B): tests de game_judge_hit, star power, rock meter y FSM. */

    printf(fails ? "%d test(s) fallaron\n" : "OK\n", fails);
    return fails != 0;
}
