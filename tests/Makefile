# Compila la lógica portable de Sep Hero en el PC (gcc de MinGW, MSYS2 o WSL).
#   make -C tests          -> compila tests/build/run_tests
#   make -C tests run      -> compila y ejecuta los tests
# En Windows sirve gmake o mingw32-make (Strawberry Perl) desde Git Bash.
# Solo toma los módulos portables; los drivers de Xilinx quedan fuera y se reemplazan
# por stubs.c (hardware).

CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -g -I../sw/src -I../sw/src/lib
SRC     := ../sw/src
PORTABLE := game_manager.c state_machine.c sound_manager.c sd_manager_formats.c \
            sd_manager_songs.c debouncer_ip_logic.c uart_config.c
COMMON  := $(addprefix $(SRC)/,$(PORTABLE)) stubs.c
BUILD   := build

$(BUILD)/run_tests: $(COMMON) $(wildcard test_*.c) $(wildcard *.h) $(wildcard $(SRC)/*.h) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON) $(wildcard test_*.c) -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(BUILD)/run_tests
	./$(BUILD)/run_tests


clean:
	rm -rf $(BUILD)

.PHONY: run sim clean
