#!/bin/sh
# =============================================================================
# make_boot.sh - Genera boot/BOOT.bin (FSBL + bitstream + sep_hero.elf) para el
# arranque autónomo de la Zybo (AC4) y, opcionalmente, lo graba en la flash QSPI.
#
# Sep Hero (IEE2463, Proyecto 2). Autores: grupo Sep Hero.
# Fuente: BIF de la AYU10 (theremin_system/Debug/system.bif) y manual §11.
#
# Uso (Git Bash):
#   sh sw/scripts/make_boot.sh           -> solo genera boot/BOOT.bin (app Release)
#   sh sw/scripts/make_boot.sh flash     -> además lo graba en la QSPI por JTAG
#
# Para grabar, el jumper JP5 debe estar en JTAG. Después: apagar, JP5 en QSPI y
# encender. El mismo BOOT.bin sirve para una microSD FAT32 (JP5 en SD).
# OJO: la imagen Release tiene el buzzer habilitado.
# =============================================================================
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WS="$ROOT/sw/workspace"
VITIS="/d/Xilinx/Vitis/2020.1"
FSBL="$WS/sephero_platform/export/sephero_platform/sw/sephero_platform/boot/fsbl.elf"
BIT="$(ls "$WS"/sephero_platform/hw/*.bit | head -1)"
ELF="$WS/sep_hero/cli_Release/sep_hero.elf"
for f in "$FSBL" "$BIT" "$ELF"; do [ -f "$f" ] || { echo "falta $f"; exit 1; }; done

# Copias con nombres fijos junto al .bif, para que el BIF sea relativo y versionable.
mkdir -p "$ROOT/boot"
cp "$FSBL" "$ROOT/boot/zynq_fsbl.elf"
cp "$BIT"  "$ROOT/boot/design_1_wrapper.bit"
cp "$ELF"  "$ROOT/boot/sep_hero.elf"
cd "$ROOT/boot" || exit 1
cmd //c "D:\\Xilinx\\Vitis\\2020.1\\bin\\bootgen.bat -image sep_hero.bif -arch zynq -o BOOT.bin -w on" || exit 1
ls -la BOOT.bin
echo "BOOT_OK $ROOT/boot/BOOT.bin"

if [ "$1" = "flash" ]; then
  cmd //c "D:\\Xilinx\\Vitis\\2020.1\\bin\\program_flash.bat -f BOOT.bin -offset 0 -flash_type qspi-x4-single -fsbl zynq_fsbl.elf -verify" || exit 1
  echo "FLASH_OK"
fi
