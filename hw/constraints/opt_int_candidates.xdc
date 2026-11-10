## opt_int_candidates.xdc - Candidatos del pin INT del OPT3001 -> axi_gpio_1 (17 bits).
## Sep Hero (IEE2463, Proyecto 2). Orden = OPT_CAND de hw/scripts/02_build_bd.tcl y
## OPT_CAND_NAME de sw/src/opt3001.c. El firmware detecta en el arranque cual sigue al INT.
## JB no existe en la Zybo Z7-10 (banco 13 sin bonding).
set_property -dict { PACKAGE_PIN U14 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[0] }];  # JD7
set_property -dict { PACKAGE_PIN U15 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[1] }];  # JD8
set_property -dict { PACKAGE_PIN V15 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[2] }];  # JC1
set_property -dict { PACKAGE_PIN W15 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[3] }];  # JC2
set_property -dict { PACKAGE_PIN T11 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[4] }];  # JC3
set_property -dict { PACKAGE_PIN T10 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[5] }];  # JC4
set_property -dict { PACKAGE_PIN U17 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[6] }];  # JE8
set_property -dict { PACKAGE_PIN T17 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[7] }];  # JE9
set_property -dict { PACKAGE_PIN Y17 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[8] }];  # JE10
set_property -dict { PACKAGE_PIN N15 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[9] }];  # JA1
set_property -dict { PACKAGE_PIN L14 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[10] }];  # JA2
set_property -dict { PACKAGE_PIN K16 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[11] }];  # JA3
set_property -dict { PACKAGE_PIN K14 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[12] }];  # JA4
set_property -dict { PACKAGE_PIN N16 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[13] }];  # JA7
set_property -dict { PACKAGE_PIN L15 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[14] }];  # JA8
set_property -dict { PACKAGE_PIN J16 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[15] }];  # JA9
set_property -dict { PACKAGE_PIN J14 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[16] }];  # JA10
