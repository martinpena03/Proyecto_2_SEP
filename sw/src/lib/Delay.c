/*
 * Delay.c - Retardos bloqueantes para el driver del LCD.
 *
 * Origen: Delay.c del driver LCD del curso (AYU11 2026-2 / LAB10, Proyecto_Base_Booster).
 * Cambios de Sep Hero: se quitó la copia local de usleep (xusleep, basada en el contador
 * de ciclos CP15) y el #include <unistd.h>, porque en el BSP de Vitis 2020.1 chocan con
 * sleep.h (dos prototipos distintos de usleep). Ahora todo usa usleep() del BSP.
 *
 * Solo se usan durante la inicialización del LCD (reset y sleep-out), nunca en el
 * bucle del juego.
 */
#include "sleep.h"
#include "Delay.h"

int xusleep(unsigned int useconds)
{
    usleep(useconds);
    return 0;
}

void millisleep(unsigned int milliseconds)
{
    unsigned int i;
    for (i = 0; i < milliseconds; i++) usleep(1000);
}

void del_sleep(unsigned int seconds)
{
    unsigned int i;
    for (i = 0; i < seconds; i++) usleep(1000000);
}

void delay_ms(unsigned int milliseconds)
{
    unsigned int i;
    for (i = 0; i < milliseconds; i++) usleep(1000);
}
