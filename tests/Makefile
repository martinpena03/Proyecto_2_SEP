# Compila la lógica portable del juego en el PC (gcc de MinGW o WSL).
#   make -C tests          -> compila tests/build/run_tests
#   make -C tests run      -> compila y ejecuta
# Solo toma los módulos portables (dueño B); los drivers de Xilinx quedan fuera.

CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -g -I../sw/src
PORTABLE := game_engine.c game_fsm.c render.c audio_seq.c sd_formats.c debouncer_logic.c
SRCS    := $(wildcard $(addprefix ../sw/src/,$(PORTABLE))) stubs.c $(wildcard test_*.c)
BUILD   := build

$(BUILD)/run_tests: $(SRCS) | $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(BUILD)/run_tests
	./$(BUILD)/run_tests

clean:
	rm -rf $(BUILD)

.PHONY: run clean
