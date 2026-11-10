#ifndef __I2C_H
#define __I2C_H
#include <stdio.h>
#include "platform.h"
#include "xil_printf.h"
#include "xiic.h"
#define IIC_dev 			XPAR_IIC_0_DEVICE_ID				//I2C DEVIDE ID
#define TMP_ADDR			0x40								//TMP SENSOR SLAVE ADDRESS
#define OPT_ADDR			0x44								//LIGHT SENSOR SLAVE ADDRESS


int init_IIC();
int read_tmp();
float read_opt();											//ILUMINACION EN LUX
int init_OPT();												//CONFIGURA EL OPT3001 (0xC410) Y LO VERIFICA
float read_tmp_float();										//TEMPERATURA CON DECIMALES (RESOLUCION 1/32 C)
extern u16 OPT_raw;											//ULTIMO REGISTRO DE RESULTADO DEL OPT3001

#endif
