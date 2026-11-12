/*
 * uart1.c - Acceso mínimo a la UART1 del PS (consola del PC, 115200 8N1).
 *
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 * Fuente: uso de xil_printf / lectura de la UART de la AYU09 (helloworld.c); aquí la
 *   lectura es no bloqueante para que el juego no se detenga (manual §8.7).
 */
#include "xparameters.h"
#include "xuartps_hw.h"
#include "xil_printf.h"
#include "hw.h"

/* Devuelve el siguiente byte recibido o -1 si no hay ninguno. */
int uart_hal_getc(void)
{
    if (!XUartPs_IsReceiveData(STDIN_BASEADDRESS)) return -1;
    return (int)XUartPs_ReadReg(STDIN_BASEADDRESS, XUARTPS_FIFO_OFFSET);
}

void uart_hal_puts(const char *s)
{
    xil_printf("%s", s);
}
