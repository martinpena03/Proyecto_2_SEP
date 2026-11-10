# 00_sim_ip_run.tcl - Comandos de xsim para el testbench del IP (lo usa 00_sim_ip.ps1).
# Guarda en un VCD las señales del IP visibles desde el testbench (para las figuras del
# informe) y corre hasta que el testbench detiene el reloj.
# Nota: no se usa "log_wave -r /": xsim 2020.1 se cae al registrar los contadores
# (arreglos de natural) del monitor del testbench.
open_vcd tb_debouncer_axi.vcd
foreach s {clk rstn btn_in btn_virt irq dbg_state dbg_edge dbg_tick
           awaddr awvalid awready wdata wvalid wready bvalid bready
           araddr arvalid arready rdata rvalid rready} {
    log_vcd /tb_debouncer_axi/$s
}
run all
close_vcd
quit
