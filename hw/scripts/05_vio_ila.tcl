# =============================================================================
# 05_vio_ila.tcl - Maneja el VIO y el ILA del diseño desde Vivado en modo batch (AC5).
#
# Sep Hero (IEE2463, Proyecto 2). Autores: grupo Sep Hero.
# Fuente: comandos del Hardware Manager vistos en la AY04 (ILA + VIO), llevados a Tcl;
#   el flujo de programar y leer el VIO por script sigue 04_program_and_check.tcl del
#   Proyecto 1. Experimentos de docs/MANUAL_IMPLEMENTACION.md §6.2.
#
# Requiere la placa ya programada y con sep_hero corriendo (el debug hub usa FCLK_CLK0).
#
# Uso:  vivado -mode batch -source hw/scripts/05_vio_ila.tcl -tclargs <ltx> <comando> [args]
#   list                      lista las sondas del VIO y del ILA
#   press <mascara> [ms]      "presiona" botones virtuales (bit0 S1, bit1 S2, bit2 SEL)
#                             durante ms (por defecto 150) y muestra STATE / EDGE
#   seq <m1,m2,...> [pausa]   varias pulsaciones seguidas, con 'pausa' ms entre ellas
#   debounce <mascara> <csv>  experimento 2: ILA con una muestra por ms (capture control
#                             con dbg_tick), trigger en dbg_edge != 0; pulsa por VIO y
#                             guarda la captura en <csv>
#   raw <mascara> <csv>       captura ciclo a ciclo (20 ns) alrededor del cambio de STATE
# =============================================================================
set LTX [lindex $argv 0]
set CMD [lindex $argv 1]

open_hw_manager
connect_hw_server -quiet
open_hw_target -quiet
set dev [lindex [get_hw_devices xc7z*] 0]
current_hw_device $dev
set_property PROBES.FILE $LTX $dev
set_property FULL_PROBES.FILE $LTX $dev
refresh_hw_device $dev

set vio [lindex [get_hw_vios -of_objects $dev] 0]
set ila [lindex [get_hw_ilas -of_objects $dev] 0]
if {$vio eq "" || $ila eq ""} { puts "ERROR: no se encontraron los cores VIO/ILA"; exit 1 }

# Busca una sonda por un trozo de su nombre.
proc probe {core pat} {
    set p [get_hw_probes -of_objects $core -filter "NAME =~ \"*$pat*\""]
    if {[llength $p] == 0} { error "sonda no encontrada: $pat" }
    return [lindex $p 0]
}
set v_out   [probe $vio probe_out0]
set v_state [probe $vio dbg_state]
set v_edge  [probe $vio dbg_edge]
set_property OUTPUT_VALUE_RADIX HEX $v_out
set_property INPUT_VALUE_RADIX HEX $v_state
set_property INPUT_VALUE_RADIX HEX $v_edge

proc vio_set {val} {
    global v_out
    set_property OUTPUT_VALUE [format %X $val] $v_out
    commit_hw_vio $v_out
}
proc vio_show {tag} {
    global vio v_state v_edge
    refresh_hw_vio $vio
    puts "VIO $tag: STATE=[get_property INPUT_VALUE $v_state] EDGE=[get_property INPUT_VALUE $v_edge]"
}
proc press {mask ms} {
    vio_set $mask
    after $ms
    vio_show "presionado 0x[format %X $mask]"
    vio_set 0
    after 100
    vio_show "soltado"
}

switch $CMD {
    list {
        foreach p [get_hw_probes -of_objects $vio] { puts "VIO_PROBE [get_property NAME $p] [get_property TYPE $p]" }
        foreach p [get_hw_probes -of_objects $ila] { puts "ILA_PROBE [get_property NAME $p]" }
        vio_show "actual"
    }
    press {
        set ms 150
        if {[llength $argv] > 3} { set ms [lindex $argv 3] }
        press [lindex $argv 2] $ms
    }
    seq {
        set gap 700
        if {[llength $argv] > 3} { set gap [lindex $argv 3] }
        foreach m [split [lindex $argv 2] ,] { press $m 150; after $gap }
    }
    debounce -
    raw {
        set mask [lindex $argv 2]
        set csv  [lindex $argv 3]
        set p_edge  [probe $ila dbg_edge]
        set p_state [probe $ila dbg_state]
        set p_tick  [probe $ila dbg_tick]
        set_property CONTROL.DATA_DEPTH 4096 $ila
        foreach p [get_hw_probes -of_objects $ila] {
            catch {set_property TRIGGER_COMPARE_VALUE eq[get_property WIDTH $p]'hX $p}
            catch {set_property CAPTURE_COMPARE_VALUE eq[get_property WIDTH $p]'hX $p}
        }
        if {$CMD eq "debounce"} {
            # Una muestra por milisegundo: ventana de 4,1 s. Trigger: algún flanco pendiente.
            set_property CONTROL.CAPTURE_MODE BASIC $ila
            set_property CAPTURE_COMPARE_VALUE eq1'b1 $p_tick
            set_property TRIGGER_COMPARE_VALUE neq3'h0 $p_edge
            set_property CONTROL.TRIGGER_POSITION 500 $ila
        } else {
            # Ciclo a ciclo (82 us): trigger cuando el nivel filtrado deja de ser 0.
            set_property CONTROL.CAPTURE_MODE ALWAYS $ila
            set_property TRIGGER_COMPARE_VALUE neq3'h0 $p_state
            set_property CONTROL.TRIGGER_POSITION 2048 $ila
        }
        run_hw_ila $ila
        after 700
        vio_set $mask
        after 300
        vio_show "presionado 0x[format %X $mask]"
        vio_set 0
        wait_on_hw_ila -timeout 1 $ila
        upload_hw_ila_data $ila
        write_hw_ila_data -csv_file -force $csv [current_hw_ila_data]
        puts "INFO: ILA_CSV $csv"
    }
    default { puts "ERROR: comando desconocido $CMD" }
}
close_hw_target -quiet
disconnect_hw_server -quiet
puts "INFO: VIO_ILA_OK"
