#include "I2C.h"

XIic  iic;
static u8 SendBuffer[3];
static u8 RecvBuffer[2];
float Lux;
u16 OPT_raw;

int init_IIC()
{
    XIic_Config *iic_conf = XIic_LookupConfig(IIC_dev);
    if (iic_conf == NULL) return XST_DEVICE_NOT_FOUND;
    if (XIic_CfgInitialize(&iic, iic_conf, iic_conf->BaseAddress) != XST_SUCCESS) return XST_FAILURE;
    XIic_Start(&iic);
    return XST_SUCCESS;
}

/* Leer sensor de luz */
u16 opt_read_reg(u8 reg)
{
    SendBuffer[0] = reg;
    XIic_Send(iic.BaseAddress, OPT_ADDR, (u8 *)&SendBuffer, 1, XIIC_REPEATED_START);
    XIic_Recv(iic.BaseAddress, OPT_ADDR, (u8 *)&RecvBuffer, 2, XIIC_STOP);
    return (u16)((RecvBuffer[0] << 8) | RecvBuffer[1]);
}

/* escribe un registro del sensor de luz */
int opt_write_reg(u8 reg, u16 val)
{
    SendBuffer[0] = reg;
    SendBuffer[1] = (u8)(val >> 8);
    SendBuffer[2] = (u8)(val & 0xFFu);
    return (XIic_Send(iic.BaseAddress, OPT_ADDR, (u8 *)&SendBuffer, 3, XIIC_STOP) == 3) ? 0 : -1;
}

int init_OPT()
{

    u16 id, leido;
    int intento;

    id = opt_read_reg(0x7E);
    xil_printf("Sensor de luz inicializado \r\n");
    for (intento = 0; intento < 5; intento++) {
        (void)opt_write_reg(0x01, 0xC410);
        leido = opt_read_reg(0x01);
        xil_printf("OPT3001: config leida = 0x%04x\r\n", leido);
        if ((leido & 0xFE00) == 0xC400) {
            return XST_SUCCESS;
        }
    }
    return XST_FAILURE;
}

float read_opt()
{
    OPT_raw = opt_read_reg(0x00);
    /* lAplicamos la formula del enunciado para la luz  */
    Lux = 0.01f * (float)(1u << (OPT_raw >> 12)) * (float)(OPT_raw & 0x0FFF);
    return Lux;
}
