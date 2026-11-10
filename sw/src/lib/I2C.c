/*
 * I2C.c - Maestro I2C (AXI IIC) y acceso básico al sensor de luz OPT3001.
 *
 * Origen: I2C.c de la AYU10 (I2C_BUZZER/sw/comun), a su vez basado en el del
 * Proyecto_Base_Booster del LAB10.
 * Cambios de Sep Hero:
 *   - init_IIC() ya no llama a init_platform() ni configura el TMP006 (no se usa);
 *     ahora revisa los códigos de retorno.
 *   - se agregan opt_read_reg() / opt_write_reg() públicos, que usa opt3001.c para
 *     escribir CONFIG, LOW_LIMIT y HIGH_LIMIT (AO1: modificar >= 2 registros).
 * Se conservan init_OPT() y read_opt() del curso.
 *
 * Regla del proyecto: el I2C se usa SOLO desde main (nunca desde una ISR).
 */
#include "I2C.h"

XIic  iic;
static u8 SendBuffer[3];
static u8 RecvBuffer[2];
float Lux;                                  /* iluminación en lux (última lectura)     */
u16 OPT_raw;                                /* último registro RESULT leído            */

int init_IIC()
{
    XIic_Config *iic_conf = XIic_LookupConfig(IIC_dev);
    if (iic_conf == NULL) return XST_DEVICE_NOT_FOUND;
    if (XIic_CfgInitialize(&iic, iic_conf, iic_conf->BaseAddress) != XST_SUCCESS) return XST_FAILURE;
    XIic_Start(&iic);
    return XST_SUCCESS;
}

/* Lee un registro de 16 bits del OPT3001 (MSB primero). */
u16 opt_read_reg(u8 reg)
{
    SendBuffer[0] = reg;
    XIic_Send(iic.BaseAddress, OPT_ADDR, (u8 *)&SendBuffer, 1, XIIC_REPEATED_START);
    XIic_Recv(iic.BaseAddress, OPT_ADDR, (u8 *)&RecvBuffer, 2, XIIC_STOP);
    return (u16)((RecvBuffer[0] << 8) | RecvBuffer[1]);
}

/* Escribe un registro de 16 bits del OPT3001. Devuelve 0 si se enviaron los 3 bytes. */
int opt_write_reg(u8 reg, u16 val)
{
    SendBuffer[0] = reg;
    SendBuffer[1] = (u8)(val >> 8);
    SendBuffer[2] = (u8)(val & 0xFFu);
    return (XIic_Send(iic.BaseAddress, OPT_ADDR, (u8 *)&SendBuffer, 3, XIIC_STOP) == 3) ? 0 : -1;
}

int init_OPT()
{
    /* Escribe 0xC410 en el registro de configuración 0x01 y lo relee para confirmar
     * que quedó. Si la escritura se pierde, el OPT3001 sigue en su modo por defecto
     * (shutdown) y el resultado queda siempre en 0 lux. */
    u16 id, leido;
    int intento;

    id = opt_read_reg(0x7E);                                    /* MANUFACTURER ID: 0x5449 ("TI") */
    xil_printf("OPT3001: ID fabricante = 0x%04x (esperado 0x5449)\r\n", id);
    for (intento = 0; intento < 5; intento++) {
        (void)opt_write_reg(0x01, 0xC410);
        leido = opt_read_reg(0x01);
        xil_printf("OPT3001: config leida = 0x%04x\r\n", leido);
        if ((leido & 0xFE00) == 0xC400) {                       /* bits 8..0: banderas y modo */
            return XST_SUCCESS;
        }
    }
    return XST_FAILURE;
}

float read_opt()
{
    OPT_raw = opt_read_reg(0x00);                               /* RESULT */
    /* lux = 0.01 * 2^E * R, con E = bits 15..12 (exponente) y R = bits 11..0 (mantisa) */
    Lux = 0.01f * (float)(1u << (OPT_raw >> 12)) * (float)(OPT_raw & 0x0FFF);
    return Lux;
}
