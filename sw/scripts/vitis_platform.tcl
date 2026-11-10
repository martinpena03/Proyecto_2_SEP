# =============================================================================
# vitis_platform.tcl - Crea (o actualiza) el workspace de Vitis de Sep Hero:
# plataforma standalone con FSBL, BSP con xilffs y la aplicación vacía sep_hero.
#
# Sep Hero (IEE2463, Proyecto 2). Basado en 03_vitis_build.tcl de la AYU10.
#
# Uso (Vitis 2020.1):
#   xsct sw/scripts/vitis_platform.tcl                 -> usa hw/export/sephero.xsa
#   xsct sw/scripts/vitis_platform.tcl <ruta.xsa>      -> usa otro hardware
#
# El workspace queda en sw/workspace (ignorado por git). La aplicación se compila con
# sw/scripts/build_app.sh (el "app build" de xsct 2020.1 se cae en esta máquina) o
# desde la GUI de Vitis importando sw/src como carpeta enlazada.
# =============================================================================
set ROOT [file normalize [file join [file dirname [info script]] .. ..]]
set WS   $ROOT/sw/workspace
set XSA  $ROOT/hw/export/sephero.xsa
if {[llength $argv] > 0} { set XSA [file normalize [lindex $argv 0]] }
set PLAT sephero_platform
set APP  sep_hero
puts "INFO: XSA = $XSA"
setws $WS

if {[catch {platform list} plist] || [string first $PLAT $plist] < 0} {
    # Plataforma nueva: genera también el FSBL (necesario para el BOOT.bin, AC4).
    platform create -name $PLAT -hw $XSA -proc ps7_cortexa9_0 -os standalone
    domain active standalone_domain
    # FatFs de Xilinx para la microSD (AC7): interfaz SD, lectura/escritura, nombres 8.3.
    bsp setlib -name xilffs
    bsp config fs_interface 1
    bsp config read_only false
    bsp config use_lfn 0
    # Consola por la UART1 del PS (USB-UART de la Zybo).
    bsp config stdin  ps7_uart_1
    bsp config stdout ps7_uart_1
    platform write
} else {
    platform active $PLAT
    platform config -updatehw $XSA
}
platform generate

if {[catch {app list} alist] || [string first $APP $alist] < 0} {
    app create -name $APP -platform $PLAT -domain standalone_domain -template {Empty Application}
}
puts "INFO: PLATAFORMA_OK $WS/$PLAT"
