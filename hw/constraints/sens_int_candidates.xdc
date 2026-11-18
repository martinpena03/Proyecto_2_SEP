## sens_int_candidates.xdc - Pines candidatos de las interrupciones de sensores -> axi_gpio_1
## (puerto SENS_INT, 8 bits): DRDY del TMP006 (temperatura, AO2) e INT del OPT3001.
## Sep Hero (IEE2463, Proyecto 2). Orden = SENS_CAND de hw/scripts/02_build_bd.tcl y
## sens_cand_name() de sw/src/temperature_aconditioner.c. El firmware detecta en el arranque
## que bit sigue a cada pin.
## Solo el Pmod JA: la tarjeta de extension no lleva estos pines a ningun Pmod, asi que se
## traen con un cable desde la BoosterPack (DRDY del TMP006 = J2.11 -> JA1). Antes habia
## candidatos tambien en JC, JD y JE (los ocupa la tarjeta de extension), pero el DRDY no
## llego a ninguno.
set_property -dict { PACKAGE_PIN N15 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { SENS_INT[0] }];  # JA1
set_property -dict { PACKAGE_PIN L14 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { SENS_INT[1] }];  # JA2
set_property -dict { PACKAGE_PIN K16 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { SENS_INT[2] }];  # JA3
set_property -dict { PACKAGE_PIN K14 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { SENS_INT[3] }];  # JA4
set_property -dict { PACKAGE_PIN N16 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { SENS_INT[4] }];  # JA7
set_property -dict { PACKAGE_PIN L15 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { SENS_INT[5] }];  # JA8
set_property -dict { PACKAGE_PIN J16 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { SENS_INT[6] }];  # JA9
set_property -dict { PACKAGE_PIN J14 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { SENS_INT[7] }];  # JA10
