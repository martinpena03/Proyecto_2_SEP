#ifndef __ADC_H
#define __ADC_H
#include <stdio.h>
#include "platform.h"
#include "xil_printf.h"
#include "xparameters.h"	/* XPAR parameters */
#include "xspi.h"
#include "sleep.h"
#define SPI_DEVICE_ID_1		XPAR_AXI_QUAD_SPI_1_DEVICE_ID
#define BUFFER_SIZE_ADC		2


int init_adc(XSpi *SpiInstancePtr, u16 SpiDeviceId);
int read_joyx();
int read_joyy();
int read_acx();
int read_acy();
int read_acz();
int read_POT1();
int read_POT2();
int read_MIC();

/* Agregado de Sep Hero: canales y lecturas genericas (ver ADC.c). */
#define ADC_CH_JOYX 0u
#define ADC_CH_JOYY 1u
#define ADC_CH_ACX  2u
#define ADC_CH_ACY  3u
#define ADC_CH_ACZ  4u
#define ADC_CH_MIC  5u
#define ADC_CH_POT1 6u
#define ADC_CH_POT2 7u
extern XSpi SpiInstance1;
u16  adc_read_raw(u8 ch);
void adc_read_chain(const u8 *ch, u8 n, u16 *out);
#endif
