#include "I2C.h"
XIic  iic;
u8 SendBuffer[2];
u8 RecvBuffer[2];
u8 config[3];
float Lux;															//ILUMINACION EN LUX (float: bajo ~40 lux importan los decimales)
int16_t val;
int temp;
int Start = 1;
int init_IIC() {

		XIic_Config *iic_conf;

	    init_platform();

	    iic_conf = XIic_LookupConfig(IIC_dev);
	    XIic_CfgInitialize(&iic, iic_conf, iic_conf->BaseAddress);
	    XIic_Start(&iic);


		SendBuffer[0] = 0xfe;
		XIic_Send(iic.BaseAddress,TMP_ADDR,(u8 *)&SendBuffer, 1,XIIC_REPEATED_START);
		XIic_Recv(iic.BaseAddress,TMP_ADDR,(u8 *)&RecvBuffer, 2,XIIC_STOP);


		SendBuffer[0] = 0x02;
		XIic_Send(iic.BaseAddress,TMP_ADDR,(u8 *)&SendBuffer, 1,XIIC_STOP);

		SendBuffer[0] = 0x80;
		XIic_Send(iic.BaseAddress,TMP_ADDR,(u8 *)&SendBuffer, 1,XIIC_REPEATED_START);
		SendBuffer[0] = 0x82;
		XIic_Send(iic.BaseAddress,TMP_ADDR,(u8 *)&SendBuffer, 1,XIIC_REPEATED_START);



    return XST_SUCCESS;
}

int read_tmp(){
	SendBuffer[0] = 0x01; 																//TEMPERATURE MEASUREMENT REGISTER ADDRESS
	XIic_Send(iic.BaseAddress, TMP_ADDR, (u8 *)&SendBuffer, 1, XIIC_REPEATED_START);	//SEND FROM ZYBO TO BOOSTER THE TEMPERATURE REGISTER THROUGH SDA BUS
	XIic_Recv(iic.BaseAddress, TMP_ADDR, (u8 *)&RecvBuffer, 2, XIIC_STOP);				//RECIEVE THE TMP MEASURED FROM BOOSTER
	val = (RecvBuffer[0] << 8) | (RecvBuffer[1]);
	temp = val / 128;																	//CELSIUS DEGREES
	return temp;
}

float read_tmp_float(){
	// Igual que read_tmp(), pero sin truncar: el registro 0x01 del TMP006 tiene
	// 14 bits alineados a la izquierda con LSB = 1/32 C  ->  T = val / 128.0
	SendBuffer[0] = 0x01;
	XIic_Send(iic.BaseAddress, TMP_ADDR, (u8 *)&SendBuffer, 1, XIIC_REPEATED_START);
	XIic_Recv(iic.BaseAddress, TMP_ADDR, (u8 *)&RecvBuffer, 2, XIIC_STOP);
	val = (int16_t)((RecvBuffer[0] << 8) | (RecvBuffer[1]));
	return val / 128.0f;
}

u16 OPT_raw;																//ULTIMO REGISTRO DE RESULTADO LEIDO (DIAGNOSTICO)

static u16 opt_read_reg(u8 reg){
	SendBuffer[0] = reg;
	XIic_Send(iic.BaseAddress,OPT_ADDR,(u8 *)&SendBuffer, 1, XIIC_REPEATED_START);
	XIic_Recv(iic.BaseAddress,OPT_ADDR,(u8 *)&RecvBuffer, 2, XIIC_STOP);
	return (RecvBuffer[0] << 8) | RecvBuffer[1];
}

int init_OPT(){
	// Escribe 0xC410 en el registro de configuracion 0x01 y lo relee para
	// confirmar que quedo. Si la escritura se pierde, el OPT3001 sigue en su
	// modo por defecto (shutdown) y el resultado queda siempre en 0 lux.
	u8 config[3] = {0x01, 0xc4, 0x10};
	u16 id, leido;
	int intento;

	id = opt_read_reg(0x7E);															//MANUFACTURER ID: 0x5449 ("TI")
	xil_printf("OPT3001: ID fabricante = 0x%04x (esperado 0x5449)\r\n", id);
	for (intento = 0; intento < 5; intento++) {
		XIic_Send(iic.BaseAddress,OPT_ADDR,(u8 *)&config, 3, XIIC_STOP);
		leido = opt_read_reg(0x01);
		xil_printf("OPT3001: config leida = 0x%04x\r\n", leido);
		if ((leido & 0xFE00) == 0xC400) {												//BITS 8..0 SON BANDERAS DE SOLO LECTURA
			Start = 0;																	//read_opt() ya no necesita configurar
			return XST_SUCCESS;
		}
	}
	return XST_FAILURE;
}

float read_opt(){
	u8 config[3] = {0x01, 0xc4, 0x10};													//0X01 CONFIGURATION REGISTER
	if(Start == 1){																		//0xc410 16bits MESSAGE TO SET THE CONFIGURATION OF LIGHT SENSOR
		XIic_Send(iic.BaseAddress,OPT_ADDR,(u8 *)&config, 3, XIIC_STOP);				//LIGHT SENSOR CONFIGURATION
		Start = 0;
	}
	OPT_raw = opt_read_reg(0x00);														//RESULT REGISTER
	// lux = 0.01 * 2^E * R, con E = bits 15..12 (exponente) y R = bits 11..0 (mantisa)
	Lux = 0.01f * (float)(1u << (OPT_raw >> 12)) * (float)(OPT_raw & 0x0FFF);
	return Lux;
}
