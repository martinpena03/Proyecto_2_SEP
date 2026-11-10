/*
 * ADC.c - Lectura del ADC SPI multicanal de la tarjeta de extension (joystick,
 * acelerometro, microfono y potenciometros), por axi_quad_spi_1.
 *
 * Origen: ADC.c del LAB10 2026-2 (Proyecto_Base_Booster), sin cambios en las funciones
 * read_*() del curso. Agregado de Sep Hero (al final del archivo):
 *   - adc_read_raw(ch): lectura generica de un canal con el mismo patron del curso;
 *   - adc_read_chain(): lee varios canales en rafaga encadenada (n + 1 transferencias
 *     en vez de 2n), para acortar la interrupcion de 2 ms.
 * Se quito el xil_printf de init_adc() (ensuciaba la consola).
 */
#include "ADC.h"
XSpi SpiInstance1;
typedef u8 DataBuffer[BUFFER_SIZE_ADC];


int init_adc(XSpi *SpiInstancePtr, u16 SpiDeviceId) {

	int Status;

	XSpi_Config *ConfigPtr; /* Pointer to Configuration data */

	ConfigPtr = XSpi_LookupConfig(SpiDeviceId);
	if (ConfigPtr == NULL) {
		return XST_DEVICE_NOT_FOUND;
	}

	Status = XSpi_CfgInitialize(SpiInstancePtr, ConfigPtr,
			ConfigPtr->BaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	Status = XSpi_SetOptions(SpiInstancePtr,
			XSP_MASTER_OPTION | XSP_MANUAL_SSELECT_OPTION);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	XSpi_Start(SpiInstancePtr);

	XSpi_IntrGlobalDisable(SpiInstancePtr);

	XSpi_SetSlaveSelect(SpiInstancePtr, 0x01);
	return XST_SUCCESS;
}
int read_joyx(){
	u8 WriteBufferX[BUFFER_SIZE_ADC] = { 0x00, 0x00 };
	u8 ReadBufferX[BUFFER_SIZE_ADC] = { 0x00, 0x00 };
	u16 ejeX;
	for (int i = 0; i < 2; i++) {
		XSpi_Transfer(&SpiInstance1, WriteBufferX, ReadBufferX,
				BUFFER_SIZE_ADC);
		ejeX = (ReadBufferX[0] + ReadBufferX[1] * 256)/4;
	}
	return ejeX;
}
int read_joyy(){
	u8 WriteBufferY[BUFFER_SIZE_ADC] = { 0x08, 0x08 };
	u8 ReadBufferY[BUFFER_SIZE_ADC] = { 0x08, 0x08 };
	u16 ejeY;
	for (int i = 0; i < 2; i++) {
		XSpi_Transfer(&SpiInstance1, WriteBufferY, ReadBufferY,
				BUFFER_SIZE_ADC);
		ejeY = (ReadBufferY[0] + ReadBufferY[1] * 256)/4;
	}
	return ejeY;

}
int read_acx(){
	u8 WriteBufferACX[BUFFER_SIZE_ADC] = { 0x10, 0x10 };
	u8 ReadBufferACX[BUFFER_SIZE_ADC] = { 0x10, 0x10 };
	u16 ejeACX;
	for (int i = 0; i < 2; i++) {
		XSpi_Transfer(&SpiInstance1, WriteBufferACX, ReadBufferACX,
				BUFFER_SIZE_ADC);
		ejeACX = (ReadBufferACX[0] + ReadBufferACX[1] * 256)/4;
	}
	return ejeACX;

}
int read_acy(){
	u8 WriteBufferACY[BUFFER_SIZE_ADC] = { 0x18, 0x18 };
	u8 ReadBufferACY[BUFFER_SIZE_ADC] = { 0x18, 0x18 };
	u16 ejeACY;
	for (int i = 0; i < 2; i++) {
		XSpi_Transfer(&SpiInstance1, WriteBufferACY, ReadBufferACY,
				BUFFER_SIZE_ADC);
		ejeACY = (ReadBufferACY[0] + ReadBufferACY[1] * 256)/4;
	}
	return ejeACY;

}
int read_acz(){
	u8 WriteBufferACZ[BUFFER_SIZE_ADC] = { 0x20, 0x20 };
	u8 ReadBufferACZ[BUFFER_SIZE_ADC] = { 0x20, 0x20 };
	u16 ejeACZ;
	for (int i = 0; i < 2; i++) {
		XSpi_Transfer(&SpiInstance1, WriteBufferACZ, ReadBufferACZ,
				BUFFER_SIZE_ADC);
		ejeACZ = (ReadBufferACZ[0] + ReadBufferACZ[1] * 256)/4;
	}
	return ejeACZ;

}

int read_POT1(){
	u8 WriteBufferACZ[BUFFER_SIZE_ADC] = { 0x30, 0x30 };
	u8 ReadBufferACZ[BUFFER_SIZE_ADC] = { 0x30, 0x30 };
	u16 POT1;
	for (int i = 0; i < 2; i++) {
		XSpi_Transfer(&SpiInstance1, WriteBufferACZ, ReadBufferACZ,
				BUFFER_SIZE_ADC);
		POT1 = (ReadBufferACZ[0] + ReadBufferACZ[1] * 256)/4;
	}
	return POT1;

}

int read_POT2(){
	u8 WriteBufferACZ[BUFFER_SIZE_ADC] = { 0x38, 0x38 };
	u8 ReadBufferACZ[BUFFER_SIZE_ADC] = { 0x38, 0x38 };
	u16 POT2;
	for (int i = 0; i < 2; i++) {
		XSpi_Transfer(&SpiInstance1, WriteBufferACZ, ReadBufferACZ,
				BUFFER_SIZE_ADC);
		POT2 = (ReadBufferACZ[0] + ReadBufferACZ[1] * 256)/4;
	}
	return POT2;

}

int read_MIC(){
	u8 WriteBufferACZ[BUFFER_SIZE_ADC] = { 0x28, 0x28 };
	u8 ReadBufferACZ[BUFFER_SIZE_ADC] = { 0x28, 0x28 };
	u16 MIC;
	for (int i = 0; i < 2; i++) {
		XSpi_Transfer(&SpiInstance1, WriteBufferACZ, ReadBufferACZ,
				BUFFER_SIZE_ADC);
		MIC = (ReadBufferACZ[0] + ReadBufferACZ[1] * 256)/4;
	}
	return MIC;

}

/* ------------------------------------------------------------------------- */
/* Agregado de Sep Hero                                                       */
/* ------------------------------------------------------------------------- */

/* Una transferencia de 16 bits: envia el comando del canal 'next_ch' y devuelve la
 * conversion (10 bits) del canal pedido en la transferencia ANTERIOR. */
static u16 adc_xfer(u8 next_ch)
{
	u8 tx[BUFFER_SIZE_ADC];
	u8 rx[BUFFER_SIZE_ADC] = { 0, 0 };
	tx[0] = (u8)(next_ch << 3);                 /* el canal va en los bits 5:3 */
	tx[1] = (u8)(next_ch << 3);
	XSpi_Transfer(&SpiInstance1, tx, rx, BUFFER_SIZE_ADC);
	return (u16)((rx[0] + rx[1] * 256) / 4);    /* misma formula del curso */
}

/* Lee un canal (0..7): dos transferencias, como las funciones read_*() del curso. */
u16 adc_read_raw(u8 ch)
{
	(void)adc_xfer(ch);
	return adc_xfer(ch);
}

/* Lee n canales encadenados: mientras se recibe el canal k se pide el k+1. */
void adc_read_chain(const u8 *ch, u8 n, u16 *out)
{
	u8 k;
	(void)adc_xfer(ch[0]);
	for (k = 0; k < n; k++) {
		out[k] = adc_xfer((k + 1u < n) ? ch[k + 1u] : ch[0]);
	}
}
