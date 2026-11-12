# =============================================================================
# run_on_board.tcl - Programa la FPGA, inicializa el PS y ejecuta sep_hero por JTAG.
#
# Sep Hero (IEE2463, Proyecto 2). Basado en 04_run_on_board.tcl de la AYU10.
#
# Uso (Vitis 2020.1):
#   xsct sw/scripts/run_on_board.tcl [Debug|Sound|Release] [bootmode]
#     bootmode : solo lee e imprime el modo de boot (jumper JP5) y termina.
#
# Seguridad (regla de silencio): antes de resetear la placa lee el registro BOOT_MODE.
# Si JP5 no está en JTAG se detiene, porque un reset arrancaría la imagen grabada en la
# QSPI o en la SD (que puede tener el buzzer habilitado).
# =============================================================================
set ROOT [file normalize [file join [file dirname [info script]] .. ..]]
set HW   $ROOT/sw/workspace/sephero_platform/hw
set CFG  Debug
set ONLY_MODE 0
foreach a $argv {
    if {$a eq "bootmode"} { set ONLY_MODE 1 } else { set CFG $a }
}
set ELF  $ROOT/sw/workspace/sep_hero/cli_$CFG/sep_hero.elf   ;# salida de build_app.sh

connect
targets -set -nocase -filter {name =~ "*A9*#0"}
catch {stop}
after 200
# SLCR.BOOT_MODE (0xF800025C), bits 2:0 -> 0 = JTAG, 1 = QSPI, 5 = SD.
set bm [expr {[lindex [mrd -value 0xF800025C] 0] & 0x7}]
set name [expr {$bm == 0 ? "JTAG" : ($bm == 1 ? "QSPI" : ($bm == 5 ? "SD" : "otro"))}]
puts "INFO: BOOT_MODE = $bm ($name)"
if {$ONLY_MODE} { catch {con}; disconnect; exit 0 }
if {$bm != 0} {
    puts "ERROR: JP5 no esta en JTAG; no se resetea la placa (regla de silencio)."
    catch {con}
    disconnect
    exit 1
}

puts "INFO: BIT = [glob $HW/*.bit]"
puts "INFO: ELF = $ELF"
targets -set -nocase -filter {name =~ "APU*"}
rst -system
after 1000
targets -set -nocase -filter {name =~ "xc7z0*"}
fpga -file [glob $HW/*.bit]
targets -set -nocase -filter {name =~ "APU*"}
source $HW/ps7_init.tcl
ps7_init
ps7_post_config
targets -set -nocase -filter {name =~ "*A9*#0"}
dow $ELF
con
puts "INFO: APP_CORRIENDO"
disconnect
