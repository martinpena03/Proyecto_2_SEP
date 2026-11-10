/*
 * I2C.h - Maestro I2C (AXI IIC) y OPT3001. Origen: AYU10; ver I2C.c para los cambios.
 */
#ifndef __I2C_H
#define __I2C_H
#include <stdio.h>
#include "platform.h"
#include "xil_printf.h"
#include "xiic.h"
#define IIC_dev             XPAR_IIC_0_DEVICE_ID            /* I2C device ID                */
#define OPT_ADDR            0x44                            /* dirección del sensor de luz  */

extern XIic iic;
extern u16 OPT_raw;                                         /* último RESULT del OPT3001    */

int   init_IIC();
int   init_OPT();                                           /* configura (0xC410) y verifica */
float read_opt();                                           /* iluminación en lux            */
u16   opt_read_reg(u8 reg);
int   opt_write_reg(u8 reg, u16 val);

#endif
