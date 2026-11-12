# =============================================================================
# 02_build_bd.tcl - Crea desde cero el proyecto Vivado de Sep Hero, arma el block
# design "design_1", genera el bitstream y exporta el hardware (.xsa + .ltx).
#
# Sep Hero (IEE2463, Proyecto 2). Autores: grupo Sep Hero.
# Fuentes: estructura del block design DEMO.bd del LAB10 (PS7 + AXI Quad SPI x2 +
#   AXI GPIO + AXI IIC), flujo batch 02_update_bd_build.tcl de la AYU10 y
#   docs/MANUAL_IMPLEMENTACION.md §4 (configuración exacta de cada bloque).
#
# VERSION AVANCE (rama avance-p2): solo las conexiones del avance.
#   PS7 (UART1, QSPI, TTC0 -> EMIO -> buzzer) + axi_timer_0/1 + axi_quad_spi_1 (ADC:
#   acelerometro) + axi_iic_0 (OPT3001) + Debouncer_IP (botones S1/S2/JOY_SEL y
#   LEDs LD0..LD3 del modo claro). Sin LCD, joystick, microfono, microSD, VIO/ILA ni
#   axi_gpio_1 (el INT del OPT3001 no llega a la Zybo). El proyecto completo esta en la
#   rama proyecto-final.
#
# Uso (Vivado 2020.1, desde cualquier carpeta):
#   vivado -mode batch -source hw/scripts/02_build_bd.tcl
# Exporta hw/export/sephero.xsa y hw/bd/design_1.tcl.
# El proyecto queda en hw/vivado/sephero (ignorado por git).
# =============================================================================
set MODE avance
set ROOT  [file normalize [file join [file dirname [info script]] .. ..]]
set PROJD $ROOT/hw/vivado/sephero
set BD    design_1
set JOBS  2   ;# nunca mas de 2: con mas nucleos el PC se cae
# Vivado 2020.1 se cae (TclStackFree: incorrect freePtr) con muchas sintesis OOC multihilo.
set_param general.maxThreads 1


# ----------------------------------------------------------------------------
# Proyecto
# ----------------------------------------------------------------------------
create_project sephero $PROJD -part xc7z010clg400-1 -force
set_property board_part digilentinc.com:zybo-z7-10:part0:1.2 [current_project]
set_property target_language VHDL [current_project]
set_property ip_repo_paths [list $ROOT/hw/ip_repo] [current_project]
update_ip_catalog -rebuild

create_bd_design $BD

# ----------------------------------------------------------------------------
# Processing System (ARM Cortex-A9; se usa solo CPU0, sin MicroBlaze)
# ----------------------------------------------------------------------------
set ps [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 processing_system7_0]
# Preset de la placa: DDR, MIO (UART1 48-49, QSPI) y relojes. La microSD (SD0) no va en el avance.
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 \
    -config {make_external "FIXED_IO, DDR" apply_board_preset "1" Master "Disable" Slave "Disable"} $ps
set_property -dict [list \
    CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ {50} \
    CONFIG.PCW_USE_M_AXI_GP0 {1} \
    CONFIG.PCW_USE_FABRIC_INTERRUPT {1} \
    CONFIG.PCW_IRQ_F2P_INTR {1} \
    CONFIG.PCW_UART1_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_SD0_PERIPHERAL_ENABLE {0} \
    CONFIG.PCW_QSPI_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_TTC0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_TTC0_TTC0_IO {EMIO} \
] $ps
# Ethernet y USB no se usan: se deshabilitan para no dejar bloques sin uso.
foreach p {PCW_ENET0_PERIPHERAL_ENABLE PCW_USB0_PERIPHERAL_ENABLE} {
    if {[catch {set_property CONFIG.$p {0} $ps} msg]} { puts "AVISO: no se pudo deshabilitar $p: $msg" }
}

# ----------------------------------------------------------------------------
# Bloques de la PL
# ----------------------------------------------------------------------------
set rst [create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst_ps7_0_50M]

set xbar [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 ps7_0_axi_periph]
set_property CONFIG.NUM_MI {5} $xbar


# M00: SPI del ADC (acelerometro) de la tarjeta de extensión (16 bits). El LAB10 usa SCK_RATIO = 2, pero
# a 25 MHz el dato del ADC llega corrido un bit (medido en la placa: las lecturas salían
# divididas por 2 y con el bit alto errático). Con 16 el reloj es 3,125 MHz.
set spi1 [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_quad_spi:3.2 axi_quad_spi_1]
set_property -dict [list \
    CONFIG.C_USE_STARTUP {0} CONFIG.C_USE_STARTUP_INT {0} \
    CONFIG.C_SPI_MODE {0} CONFIG.Master_mode {1} \
    CONFIG.C_NUM_SS_BITS {1} CONFIG.C_NUM_TRANSFER_BITS {16} \
    CONFIG.C_SCK_RATIO {16} \
] $spi1

# M01: I2C del OPT3001 (100 kHz).
set iic [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_iic:2.0 axi_iic_0]
set_property CONFIG.IIC_FREQ_KHZ {100} $iic


# M02: IP propio (AO3).
set deb [create_bd_cell -type ip -vlnv uc.cl:sephero:debouncer_axi:1.0 Debouncer_IP]
set_property -dict [list CONFIG.G_WIDTH {3} CONFIG.G_TICKS_PER_MS {50000}] $deb

# M03 / M04: timers (solo el timer 0 de cada IP, 32 bits).
set tmr1 [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_timer:2.0 axi_timer_1]
set tmr0 [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_timer:2.0 axi_timer_0]
foreach t [list $tmr0 $tmr1] { set_property CONFIG.enable_timer2 {0} $t }

# Concatenador de interrupciones -> IRQ_F2P[2:0].
set cat [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat:2.1 xlconcat_0]
set_property CONFIG.NUM_PORTS {3} $cat


# ----------------------------------------------------------------------------
# AXI: maestro GP0 -> interconnect -> 5 esclavos (orden de SepHero_BD página 2)
# ----------------------------------------------------------------------------
connect_bd_intf_net [get_bd_intf_pins processing_system7_0/M_AXI_GP0] [get_bd_intf_pins ps7_0_axi_periph/S00_AXI]
set slaves {
    axi_quad_spi_1/AXI_LITE
    axi_iic_0/S_AXI
    Debouncer_IP/s00_axi
    axi_timer_1/S_AXI
    axi_timer_0/S_AXI
}
set i 0
foreach s $slaves {
    connect_bd_intf_net [get_bd_intf_pins ps7_0_axi_periph/M0${i}_AXI] [get_bd_intf_pins $s]
    incr i
}

# ----------------------------------------------------------------------------
# Reloj (FCLK_CLK0 = 50 MHz) y reset: llegan a todos los bloques
# ----------------------------------------------------------------------------
set clk_pins [list \
    processing_system7_0/M_AXI_GP0_ACLK rst_ps7_0_50M/slowest_sync_clk \
    ps7_0_axi_periph/ACLK ps7_0_axi_periph/S00_ACLK \
    axi_quad_spi_1/s_axi_aclk axi_quad_spi_1/ext_spi_clk \
    axi_iic_0/s_axi_aclk \
    axi_timer_0/s_axi_aclk axi_timer_1/s_axi_aclk \
    Debouncer_IP/s00_axi_aclk]
set rst_pins [list \
    ps7_0_axi_periph/S00_ARESETN \
    axi_quad_spi_1/s_axi_aresetn axi_iic_0/s_axi_aresetn \
    axi_timer_0/s_axi_aresetn axi_timer_1/s_axi_aresetn \
    Debouncer_IP/s00_axi_aresetn]
for {set i 0} {$i < 5} {incr i} {
    lappend clk_pins ps7_0_axi_periph/M0${i}_ACLK
    lappend rst_pins ps7_0_axi_periph/M0${i}_ARESETN
}
set cmd [list connect_bd_net -net FCLK_CLK0 [get_bd_pins processing_system7_0/FCLK_CLK0]]
foreach p $clk_pins { lappend cmd [get_bd_pins $p] }
eval $cmd
set cmd [list connect_bd_net -net peripheral_aresetn [get_bd_pins rst_ps7_0_50M/peripheral_aresetn]]
foreach p $rst_pins { lappend cmd [get_bd_pins $p] }
eval $cmd
connect_bd_net [get_bd_pins processing_system7_0/FCLK_RESET0_N] [get_bd_pins rst_ps7_0_50M/ext_reset_in]
connect_bd_net [get_bd_pins rst_ps7_0_50M/interconnect_aresetn] [get_bd_pins ps7_0_axi_periph/ARESETN]

# ----------------------------------------------------------------------------
# Interrupciones: xlconcat_0 -> IRQ_F2P[2:0] (ID del GIC 61..63)
# ----------------------------------------------------------------------------
connect_bd_net -net irq_t0 [get_bd_pins axi_timer_0/interrupt]      [get_bd_pins xlconcat_0/In0]   ;# 61: 1 ms
connect_bd_net -net irq_t1 [get_bd_pins axi_timer_1/interrupt]      [get_bd_pins xlconcat_0/In1]   ;# 62: 2 ms
connect_bd_net -net irq_deb [get_bd_pins Debouncer_IP/irq]        [get_bd_pins xlconcat_0/In2]   ;# 63: botones
connect_bd_net -net IRQ_F2P [get_bd_pins xlconcat_0/dout]            [get_bd_pins processing_system7_0/IRQ_F2P]

# ----------------------------------------------------------------------------
# Puertos externos
# ----------------------------------------------------------------------------
# ADC SPI (acelerometro) e I2C (OPT3001): interfaces completas (como DEMO.bd).
create_bd_intf_port -mode Master -vlnv xilinx.com:interface:spi_rtl:1.0 spi_rtl
connect_bd_intf_net [get_bd_intf_ports spi_rtl] [get_bd_intf_pins axi_quad_spi_1/SPI_0]
create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 iic_rtl
connect_bd_intf_net [get_bd_intf_ports iic_rtl] [get_bd_intf_pins axi_iic_0/IIC]

# Buzzer: PWM del TTC0 del PS, ruteado por EMIO.
create_bd_port -dir O BUZZER_PWM
connect_bd_net -net TTC0_WAVE0_OUT [get_bd_ports BUZZER_PWM] [get_bd_pins processing_system7_0/TTC0_WAVE0_OUT]

# Botones de la Booster -> IP propio.
create_bd_port -dir I -from 2 -to 0 btn_raw
connect_bd_net -net btn_raw [get_bd_ports btn_raw] [get_bd_pins Debouncer_IP/btn_in]

# LEDs LD0..LD3 de la Zybo <- IP propio (indicador del modo claro).
create_bd_port -dir O -from 3 -to 0 leds
connect_bd_net [get_bd_ports leds] [get_bd_pins Debouncer_IP/leds]


# ----------------------------------------------------------------------------
# Mapa de direcciones (SepHero_BD página 2)
# ----------------------------------------------------------------------------
proc sephero_addr {seg off} {
    assign_bd_address -offset $off -range 0x00010000 \
        -target_address_space [get_bd_addr_spaces processing_system7_0/Data] \
        [get_bd_addr_segs $seg] -force
}
sephero_addr axi_iic_0/S_AXI/Reg           0x41600000
sephero_addr axi_quad_spi_1/AXI_LITE/Reg   0x41E10000
sephero_addr axi_timer_0/S_AXI/Reg         0x42800000
sephero_addr axi_timer_1/S_AXI/Reg         0x42810000
sephero_addr Debouncer_IP/s00_axi/*     0x43C00000

regenerate_bd_layout
validate_bd_design
save_bd_design
puts "INFO: BD_VALIDADO_OK (avance)"

# ----------------------------------------------------------------------------
# Wrapper, restricciones e implementación
# ----------------------------------------------------------------------------
set bdfile [get_files $BD.bd]
generate_target all $bdfile
set wrapper [make_wrapper -files $bdfile -top]
add_files -norecurse $wrapper
set_property top ${BD}_wrapper [current_fileset]
update_compile_order -fileset sources_1

add_files -fileset constrs_1 -norecurse $ROOT/hw/constraints/sephero.xdc

launch_runs impl_1 -to_step write_bitstream -jobs $JOBS
wait_on_run impl_1
puts "INFO: impl_1 STATUS = [get_property STATUS [get_runs impl_1]]"
if {[get_property PROGRESS [get_runs impl_1]] != "100%"} { error "ERROR: implementacion fallida" }

open_run impl_1
file mkdir $ROOT/hw/vivado/logs
report_utilization    -file $ROOT/hw/vivado/logs/utilization_$MODE.rpt
report_timing_summary -file $ROOT/hw/vivado/logs/timing_$MODE.rpt
set wns [get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -setup]]
puts "INFO: WNS = $wns ns"
if {$wns < 0} { puts "ERROR: el diseno no cumple timing (WNS < 0)" }

# ----------------------------------------------------------------------------
# Exportación
# ----------------------------------------------------------------------------
set xsa $ROOT/hw/export/sephero.xsa
write_hw_platform -fixed -include_bit -force -file $xsa
# Block design regenerable desde el repo (lo usa hw/scripts/create_project.tcl).
open_bd_design $bdfile
write_bd_tcl -force -include_layout $ROOT/hw/bd/design_1.tcl
puts "INFO: XSA_OK $xsa"
