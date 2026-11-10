# =============================================================================
# 01_package_ip.tcl - Empaqueta el IP propio debouncer_axi (AXI4-Lite + irq) en
# hw/ip_repo/debouncer_axi/ (genera component.xml y xgui/).
#
# Sep Hero (IEE2463, Proyecto 2). Basado en 01_package_ip.tcl de la AYU10
# (empaquetado del IP BUZZER_AXI), adaptado al IP debouncer_axi.
#
# Uso (Vivado 2020.1):
#   vivado -mode batch -source hw/scripts/01_package_ip.tcl
#
# Cada ejecución SUBE la core revision: sin eso Vivado no refresca el IP en el
# block design (ver README, regla 3).
# =============================================================================
set ROOT  [file normalize [file join [file dirname [info script]] .. ..]]
set IPDIR $ROOT/hw/ip_repo/debouncer_axi
set SRC   $IPDIR/src

create_project -in_memory -part xc7z010clg400-1
set_property target_language VHDL [current_project]
# Solo los fuentes sintetizables: el testbench no forma parte del IP.
add_files [list $SRC/debouncer_core.vhd $SRC/debouncer_axi.vhd]
set_property top debouncer_axi [current_fileset]
update_compile_order -fileset sources_1

# Revisión siguiente a la del component.xml existente (si lo hay).
set rev 1
if {[file exists $IPDIR/component.xml]} {
    ipx::open_core $IPDIR/component.xml
    set rev [expr {[get_property core_revision [ipx::current_core]] + 1}]
    ipx::unload_core [ipx::current_core]
    file delete $IPDIR/component.xml
}

ipx::package_project -root_dir $IPDIR -vendor uc.cl -library sephero -taxonomy /UserIP -force
set core [ipx::current_core]
set_property name          debouncer_axi $core
set_property version       1.0 $core
set_property display_name  {debouncer_axi (Sep Hero)} $core
set_property description   {Antirrebote configurable de 3 botones (S1, S2, JOY_SEL) con detector de flanco, registros AXI4-Lite e interrupcion de nivel. IP propio de Sep Hero (AO3).} $core
set_property vendor_display_name {Sep Hero - IEE2463 UC} $core
set_property core_revision $rev $core

# --- Interfaz de interrupción sobre el puerto irq (nivel alto). Sin esto el BSP no
#     genera XPAR_FABRIC_DEBOUNCER_AXI_0_IRQ_INTR.
if {[llength [ipx::get_bus_interfaces irq -of_objects $core]] == 0} {
    ipx::add_bus_interface irq $core
    set bi [ipx::get_bus_interfaces irq -of_objects $core]
    set_property abstraction_type_vlnv xilinx.com:signal:interrupt_rtl:1.0 $bi
    set_property bus_type_vlnv         xilinx.com:signal:interrupt:1.0     $bi
    set_property interface_mode        master                               $bi
    ipx::add_port_map INTERRUPT $bi
    set_property physical_name irq [ipx::get_port_maps INTERRUPT -of_objects $bi]
}
set bi [ipx::get_bus_interfaces irq -of_objects $core]
if {[llength [ipx::get_bus_parameters SENSITIVITY -of_objects $bi]] == 0} {
    ipx::add_bus_parameter SENSITIVITY $bi
}
set_property value LEVEL_HIGH [ipx::get_bus_parameters SENSITIVITY -of_objects $bi]

# --- Memory map del esclavo AXI (si package_project no lo infirió).
if {[llength [ipx::get_memory_maps s00_axi -of_objects $core]] == 0} {
    ipx::add_memory_map s00_axi $core
    set_property slave_memory_map_ref s00_axi [ipx::get_bus_interfaces s00_axi -of_objects $core]
    ipx::add_address_block reg0 [ipx::get_memory_maps s00_axi -of_objects $core]
    set_property range 4096 [ipx::get_address_blocks reg0 -of_objects [ipx::get_memory_maps s00_axi -of_objects $core]]
}

puts "INFO: bus_interfaces = [get_property NAME [ipx::get_bus_interfaces -of_objects $core]]"
puts "INFO: memory_maps    = [get_property NAME [ipx::get_memory_maps -of_objects $core]]"
puts "INFO: parametros     = [get_property NAME [ipx::get_user_parameters -of_objects $core]]"
puts "INFO: core_revision  = [get_property core_revision $core]"

ipx::create_xgui_files $core
ipx::update_checksums $core
ipx::check_integrity $core
ipx::save_core $core
puts "INFO: IP_EMPAQUETADO_OK"
