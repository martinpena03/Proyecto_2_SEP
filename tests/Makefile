# Compila la lógica portable de Sep Hero en el PC (gcc de MinGW, MSYS2 o WSL).
#   make -C tests          -> compila tests/build/run_tests
#   make -C tests run      -> compila y ejecuta los tests
#   make -C tests sim      -> genera las pantallas del simulador en sim/out/*.ppm
# En Windows sirve gmake o mingw32-make (Strawberry Perl) desde Git Bash.
# Solo toma los módulos portables; los drivers de Xilinx quedan fuera y se reemplazan
# por stubs.c (hardware) y sim/fb.c (LCD).

CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -g -I../sw/src -I../sw/src/lib
SRC     := ../sw/src
PORTABLE := game_engine.c game_fsm.c render.c text.c arrows.c audio_seq.c sd_formats.c \
            song_embedded.c debouncer_logic.c joy_logic.c uart_config.c
FONTS   := $(addprefix $(SRC)/lib/,font8.c font12.c font16.c font20.c font24.c)
COMMON  := $(addprefix $(SRC)/,$(PORTABLE)) $(FONTS) stubs.c ../sim/fb.c
BUILD   := build

$(BUILD)/run_tests: $(COMMON) $(wildcard test_*.c) $(wildcard *.h) $(wildcard $(SRC)/*.h) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON) $(wildcard test_*.c) -o $@

$(BUILD)/run_sim: $(COMMON) ../sim/main_sim.c $(wildcard *.h) $(wildcard $(SRC)/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -I. $(COMMON) ../sim/main_sim.c -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(BUILD)/run_tests
	./$(BUILD)/run_tests

sim: $(BUILD)/run_sim
	mkdir -p ../sim/out
	./$(BUILD)/run_sim ../sim/out

clean:
	rm -rf $(BUILD)

.PHONY: run sim clean
