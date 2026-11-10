## =============================================================================
## sephero.xdc - Restricciones de pines de Sep Hero (Zybo Z7-10 + tarjeta de
## extensión del curso + BoosterPack BOOSTXL-EDUMKII).
##
## Sep Hero (IEE2463, Proyecto 2). Basado en Zybo-Z7-Master.xdc (Digilent) y en los
## .xdc del LAB10 (LCD y ADC), de la AYU10 (I2C, buzzer y botones) y de la AYU11.
## Los nombres de puerto coinciden con los de design_1_wrapper.
## =============================================================================

## ---- LCD ST7735S (Pmod JE): axi_quad_spi_0 + axi_gpio_0 - igual que LAB10/AYU11
set_property -dict { PACKAGE_PIN V12 IOSTANDARD LVCMOS33 } [get_ports { RS_tri_o[0] }];   # JE1  DC/RS
set_property -dict { PACKAGE_PIN W16 IOSTANDARD LVCMOS33 } [get_ports { CS[0] }];         # JE2  CS
set_property -dict { PACKAGE_PIN J15 IOSTANDARD LVCMOS33 } [get_ports { MOSI }];          # JE3  MOSI
set_property -dict { PACKAGE_PIN H15 IOSTANDARD LVCMOS33 } [get_ports { RST_tri_o[0] }];  # JE4  RST
set_property -dict { PACKAGE_PIN V13 IOSTANDARD LVCMOS33 } [get_ports { SCLK }];          # JE7  SCLK

## ---- ADC SPI de la tarjeta de extensión (Pmod JC): axi_quad_spi_1 - igual que LAB10
set_property -dict { PACKAGE_PIN W14 IOSTANDARD LVCMOS33 } [get_ports { spi_rtl_io1_io }];    # JC7  MISO
set_property -dict { PACKAGE_PIN Y14 IOSTANDARD LVCMOS33 } [get_ports { spi_rtl_io0_io }];    # JC8  MOSI
set_property -dict { PACKAGE_PIN T12 IOSTANDARD LVCMOS33 } [get_ports { spi_rtl_sck_io }];    # JC9  SCK
set_property -dict { PACKAGE_PIN U12 IOSTANDARD LVCMOS33 } [get_ports { spi_rtl_ss_io[0] }];  # JC10 CS

## ---- I2C del OPT3001 (Pmod JD): axi_iic_0 - igual que AYU10
set_property -dict { PACKAGE_PIN V17 IOSTANDARD LVCMOS33 } [get_ports { iic_rtl_scl_io }];    # JD9  SCL
set_property -dict { PACKAGE_PIN V18 IOSTANDARD LVCMOS33 } [get_ports { iic_rtl_sda_io }];    # JD10 SDA

## ---- Buzzer: TTC0_WAVE0_OUT del PS por EMIO (cápsula del buzzer / AYU10)
set_property -dict { PACKAGE_PIN T14 IOSTANDARD LVCMOS33 } [get_ports { BUZZER_PWM }];        # JD1

## ---- Botones de la Booster -> debouncer_axi_0/btn_in (Pmod JD) - AYU10 (BTN0, BTN1, BTN_JY)
## Activos en bajo; el pull-up interno no estorba aunque la Booster ya tenga resistencias.
set_property -dict { PACKAGE_PIN T15 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { btn_in[0] }];  # JD2 S1
set_property -dict { PACKAGE_PIN P14 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { btn_in[1] }];  # JD3 S2
set_property -dict { PACKAGE_PIN R14 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { btn_in[2] }];  # JD4 JOY_SEL

## ---- INT del OPT3001 (open-drain, activo en bajo) -> axi_gpio_1
## El pin se agrega aquí una vez identificado con el buscador de pin
## (hw/scripts/02_build_bd.tcl en modo "finder"; ver docs/MANUAL_IMPLEMENTACION.md §15.1).

## ---- LEDs LD0..LD3 de la Zybo <- debouncer_axi_0/leds (indicador del modo claro)
set_property -dict { PACKAGE_PIN M14 IOSTANDARD LVCMOS33 } [get_ports { leds[0] }];  # LD0
set_property -dict { PACKAGE_PIN M15 IOSTANDARD LVCMOS33 } [get_ports { leds[1] }];  # LD1
set_property -dict { PACKAGE_PIN G14 IOSTANDARD LVCMOS33 } [get_ports { leds[2] }];  # LD2
set_property -dict { PACKAGE_PIN D18 IOSTANDARD LVCMOS33 } [get_ports { leds[3] }];  # LD3
