#!/bin/sh
# =============================================================================
# build_app.sh - Compila la aplicación sep_hero contra el BSP del workspace de Vitis.
#
# Sep Hero (IEE2463, Proyecto 2). Basado en build_app.sh de la AYU10 (equivale a "Build
# Project" en Vitis 2020.1; útil porque el "app build" de xsct se cae en esta máquina).
#
# Uso (Git Bash):
#   sh sw/scripts/build_app.sh debug     -> diagnóstico por UART + buzzer EN MUDO
#                                           (lo que se carga cuando el usuario no está)
#   sh sw/scripts/build_app.sh release   -> el entregable: -O2, con sonido, sin diagnóstico
#   sh sw/scripts/build_app.sh sound     -> como debug pero con sonido (pruebas con el
#                                           usuario presente)
# Variables opcionales:  EXTRA="-D..."
# Salida: sw/workspace/sep_hero/cli_<Debug|Release|Sound>/sep_hero.elf
# Requiere haber corrido antes:  xsct sw/scripts/vitis_platform.tcl
# =============================================================================
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CFG="${1:-debug}"
case "$CFG" in
  debug)   OUTN=Debug;   OPT="-O0 -g3 -DSEP_MUTE=1" ;;
  sound)   OUTN=Sound;   OPT="-O0 -g3 -DSEP_MUTE=0" ;;
  release) OUTN=Release; OPT="-O2 -DSEP_MUTE=0" ;;
  *) echo "uso: sh build_app.sh [debug|sound|release]"; exit 1 ;;
esac
export PATH="/d/Xilinx/Vitis/2020.1/gnu/aarch32/nt/gcc-arm-none-eabi/bin:$PATH"
WS="$ROOT/sw/workspace"
BSP="$WS/sephero_platform/export/sephero_platform/sw/sephero_platform/standalone_domain"
SRC="$ROOT/sw/src"
OUT="$WS/sep_hero/cli_$OUTN"   # cli_: no choca con Debug/Release de la GUI de Vitis
[ -d "$BSP/bspinclude/include" ] || { echo "falta el BSP: correr vitis_platform.tcl"; exit 1; }
mkdir -p "$OUT/obj"
rm -f "$OUT"/obj/*.o

# lscript.ld versionado (stack y heap de 64 KB); Xilinx.spec lo genera Vitis con la app.
LD="$ROOT/sw/lscript.ld"
SPEC="$WS/sep_hero/src/Xilinx.spec"
[ -f "$SPEC" ] || { echo "falta $SPEC"; exit 1; }

FLAGS="-Wall -Wextra $OPT $EXTRA -mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard \
 -ffunction-sections -fdata-sections -I$BSP/bspinclude/include -I$SRC -I$SRC/lib"
OBJS=""
for c in "$SRC"/*.c "$SRC"/lib/*.c; do
  o="$OUT/obj/$(basename "${c%.c}").o"
  arm-none-eabi-gcc $FLAGS -c "$c" -o "$o" || exit 1
  OBJS="$OBJS $o"
done
arm-none-eabi-gcc -mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard -Wl,-build-id=none \
  -specs="$SPEC" -Wl,-T -Wl,"$LD" -Wl,--gc-sections -L"$BSP/bsplib/lib" \
  -o "$OUT/sep_hero.elf" $OBJS \
  -lm -Wl,--start-group,-lxil,-lgcc,-lc,--end-group || exit 1
arm-none-eabi-size "$OUT/sep_hero.elf"
echo "ELF_OK $OUT/sep_hero.elf"
