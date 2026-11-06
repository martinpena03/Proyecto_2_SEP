# create_project.tcl - Reconstruye el proyecto Vivado de Sep Hero desde el repo.
#
# Uso (desde la raíz del repo, Vivado 2020.1):
#   vivado -mode batch -source hw/scripts/create_project.tcl
# El proyecto queda en hw/vivado/ (ignorado por git).
#
# Para guardar cambios del block design en el repo (lo hace A tras cada cambio):
#   open_bd_design [get_files sephero_bd.bd]
#   write_bd_tcl -force -include_layout hw/bd/sephero_bd.tcl
# y exportar el hardware:
#   write_hw_platform -fixed -include_bit -force hw/export/sephero.xsa

set root   [file normalize [file join [file dirname [info script]] .. ..]]
set projd  [file join $root hw vivado]
set part   xc7z010clg400-1

create_project sephero $projd -part $part -force

# Repositorio de IP propios (debouncer_axi)
set_property ip_repo_paths [file join $root hw ip_repo] [current_project]
update_ip_catalog -rebuild

# Restricciones
add_files -fileset constrs_1 [glob -nocomplain [file join $root hw constraints *.xdc]]

# Block design
set bd_tcl [file join $root hw bd sephero_bd.tcl]
if {[file exists $bd_tcl]} {
    source $bd_tcl
    set bd_file [get_files sephero_bd.bd]
    generate_target all $bd_file
    set wrapper [make_wrapper -files $bd_file -top]
    add_files -norecurse $wrapper
    set_property top sephero_bd_wrapper [current_fileset]
} else {
    puts "AVISO: falta $bd_tcl; se creó un proyecto vacío."
}
