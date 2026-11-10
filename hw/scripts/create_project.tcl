# create_project.tcl - Reconstruye el proyecto Vivado de Sep Hero desde el repo.
#
# Sep Hero (IEE2463, Proyecto 2). Autores: grupo Sep Hero.
#
# Uso (desde la raíz del repo, Vivado 2020.1):
#   vivado -mode batch -source hw/scripts/create_project.tcl
# El proyecto queda en hw/vivado/sephero (ignorado por git). Usa el block design
# exportado en hw/bd/design_1.tcl, el IP propio de hw/ip_repo y hw/constraints/*.xdc.
# No sintetiza: para generar el bitstream, abrir el proyecto y "Generate Bitstream".
#
# El block design se crea desde cero (y se sintetiza) con hw/scripts/02_build_bd.tcl,
# que además regenera hw/bd/design_1.tcl y hw/export/sephero.xsa.

set root   [file normalize [file join [file dirname [info script]] .. ..]]
set projd  [file join $root hw vivado sephero]
set part   xc7z010clg400-1

create_project sephero $projd -part $part -force
# Board files de Digilent (zybo-z7-10). Si no están instalados, el proyecto se crea igual.
if {[catch {set_property board_part digilentinc.com:zybo-z7-10:part0:1.2 [current_project]} msg]} {
    puts "AVISO: board files zybo-z7-10 no instalados ($msg)"
}
set_property target_language VHDL [current_project]

# Repositorio de IP propios (debouncer_axi)
set_property ip_repo_paths [file join $root hw ip_repo] [current_project]
update_ip_catalog -rebuild

# Restricciones
add_files -fileset constrs_1 [glob -nocomplain [file join $root hw constraints *.xdc]]

# Block design
set bd_tcl [file join $root hw bd design_1.tcl]
if {[file exists $bd_tcl]} {
    source $bd_tcl
    set bd_file [get_files design_1.bd]
    generate_target all $bd_file
    set wrapper [make_wrapper -files $bd_file -top]
    add_files -norecurse $wrapper
    set_property top design_1_wrapper [current_fileset]
    update_compile_order -fileset sources_1
} else {
    puts "AVISO: falta $bd_tcl; se creó un proyecto vacío."
}
