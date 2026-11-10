# =============================================================================
# 00_sim_ip.ps1 - Simula el testbench del IP debouncer_axi con xsim (Vivado 2020.1).
#
# Sep Hero (IEE2463, Proyecto 2). Basado en el flujo sim_tb_axi.tcl del Proyecto 1.
# Uso (desde cualquier carpeta):
#   powershell -ExecutionPolicy Bypass -File hw\scripts\00_sim_ip.ps1
# Resultado: imprime SIM_OK (y sale con 0) si el testbench reporta "TB_RESULT: PASS".
# Los archivos de trabajo quedan en hw\vivado\sim_ip (ignorado por git); la forma de
# onda queda en tb_debouncer_axi.vcd y tb_debouncer_axi_snap.wdb.
# =============================================================================
param([string]$Vivado = 'D:\Xilinx\Vivado\2020.1')

$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$src  = "$root\hw\ip_repo\debouncer_axi\src"
$work = "$root\hw\vivado\sim_ip"
New-Item -ItemType Directory -Force $work | Out-Null
Push-Location $work
try {
    & "$Vivado\bin\xvhdl.bat" "$src\debouncer_core.vhd" "$src\debouncer_axi.vhd" "$src\tb_debouncer_axi.vhd"
    if ($LASTEXITCODE -ne 0) { 'SIM_FAIL (xvhdl)'; exit 1 }
    & "$Vivado\bin\xelab.bat" -debug typical tb_debouncer_axi -s tb_debouncer_axi_snap
    if ($LASTEXITCODE -ne 0) { 'SIM_FAIL (xelab)'; exit 1 }
    # xsim interpreta la ruta como Tcl: las barras invertidas se pierden, se usan '/'.
    $tcl = ("$PSScriptRoot\00_sim_ip_run.tcl" -replace '\\', '/')
    & "$Vivado\bin\xsim.bat" tb_debouncer_axi_snap -tclbatch $tcl -log sim.log
} finally {
    Pop-Location
}
if (Select-String -Path "$work\sim.log" -Pattern 'TB_RESULT: PASS' -Quiet) {
    'SIM_OK'
    exit 0
}
'SIM_FAIL'
exit 1
