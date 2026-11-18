# CLAUDE.md: guía para una IA que trabaje en Sep Hero (rama `proyecto-final`)

Este archivo resume lo que una IA necesita saber para trabajar en el repositorio sin romper
nada. Está escrito en español, igual que el código, los comentarios y los commits del
proyecto. Si algo de aquí choca con lo que dice el código, manda el código: avisa y actualiza
este archivo.

## 1. Qué es el proyecto

**Sep Hero** es el Proyecto 2 de IEE2463 Sistemas Electrónicos Programables (PUC, 2026-2). Es
un juego de ritmo estilo DDR / Guitar Hero sobre una **Zybo Z7-10** (Zynq-7010) con la
**BoosterPack EDUMKII** en una tarjeta de extensión Pmod:

- Las notas son **flechas** que caen por 4 carriles (← ↑ ↓ →) en el LCD de 128×128. Se tocan
  con las **direcciones del joystick** (eje dominante, sin diagonales). En los menús el
  joystick sube y baja el cursor. Los botones BTN0–3 de la Zybo **no se usan**.
- **S1** = OK, **S2** = atrás / pausa, **JOY_SEL** = música ON/OFF (silencio enclavado).
- Sacudir la placa (acelerómetro) activa el *star power* (puntaje ×2) cuando la barra está llena.
- El sensor de luz OPT3001 cambia entre **modo claro y modo oscuro** (paleta del LCD y LEDs LD0–LD3).
- El micrófono mide el ruido del público: un grito en GAME OVER da un *encore* (+10 %).
- La melodía suena por el **buzzer** con PWM del TTC0. Las canciones (`SONGn.SHC`) y los récords
  (`RECORDS.TXT`) se leen de la microSD; si no hay SD se usan los charts embebidos en el `.elf`.

Hardware: PL con Vivado 2020.1 (block design `design_1` + IP propio en VHDL + VIO/ILA).
Software: bare-metal en el ARM Cortex-A9 con Vitis 2020.1 (standalone, C99, xilffs).

### Ramas

| Rama | Contenido |
|---|---|
| `main` | estructura inicial y reparto de trabajo |
| `ref_proy_2` | versión de referencia anterior (publicada) |
| `avance-p2` | entrega de avance (ecosistema reducido por UART). Vive como *worktree* en `D:\Sep_martin_pena\proyecto_sep_2_avance` |
| **`proyecto-final`** (esta) | **proyecto completo**. Todavía no está en GitHub: no hacer push sin que el usuario lo pida |

No mezclar cambios entre ramas sin que el usuario lo pida. Si cambias un nombre o un contrato
común, avisa que la otra rama puede necesitar el mismo cambio.

## 2. Reglas que no se negocian

1. **Buzzer en silencio sin el usuario.** Si cargas firmware a la placa por tu cuenta, usa la
   build `debug` (`SEP_MUTE=1`). Las builds `sound`, `release` y la de la GUI de Vitis suenan:
   pide autorización explícita antes de cargarlas o de desmutear. Antes de resetear la placa,
   confirma que el jumper **JP5 está en JTAG** (`xsct sw/scripts/run_on_board.tcl Debug bootmode`).
   Si está en QSPI o SD, un reset arranca una imagen grabada que puede sonar.
2. **Vivado con 2 núcleos como máximo:** `launch_runs -jobs` ≤ 2 y
   `set_param general.maxThreads` ≤ 2 (los scripts usan 1). Con más, el PC se cae. Nunca abras
   dos instancias de Vivado a la vez.
3. **Nombres del diagrama de bloques.** La fuente de verdad es
   `docs/diagramas/Diagrama_de_bloques_02.drawio`. Cada bloque y cada señal tiene el mismo
   nombre en el código, según la tabla de [`docs/NOMBRES_DIAGRAMA.md`](docs/NOMBRES_DIAGRAMA.md).
   Convención en C: archivos, variables, campos y funciones en minúsculas (`Game_over_signal`
   → `game_over_signal`), tipos con la grafía del diagrama (`Hit_signal`, `Game_status`) y
   constantes de enum en mayúsculas (`UI_POWER_UP_ENABLE`). Si agregas o renombras una señal,
   actualiza esa tabla. `SepHero_BD.pdf` y `Diagrama_de_bloques_02.pdf` son versiones viejas
   (`SepHero_BD.pdf` página 4 sigue sirviendo como mockup de las pantallas).
4. **Portabilidad.** `game_manager.c`, `state_machine.c`, `screen_manager.c`,
   `screen_manager_text.c`, `screen_manager_arrows.c`, `sound_manager.c`, `sd_manager_formats.c`,
   `sd_manager_songs.c`, `debouncer_ip_logic.c`, `joystick_aconditioner_logic.c` y
   `uart_config.c` **no incluyen headers de Xilinx** ni usan `printf`. Solo llaman funciones de
   `interfaces.h`. Se compilan en el PC con los stubs de `tests/stubs.c` y el framebuffer de `sim/`.
5. **Las ISR no usan la UART ni el I²C.** Leen o escriben registros rápidos y dejan banderas
   `volatile` o colas SPSC (`event_queues.c`). El trabajo lento lo hace `main`
   (`theme_task()`, `temp_task()`, `audio_user_mute_task()`).
6. **IP propio:** se edita en `hw/ip_repo/debouncer_axi/src/` y se re-empaqueta con
   `hw/scripts/01_package_ip.tcl` **subiendo la core revision**. Si no, Vivado no lo refresca.
7. **Rutas sin espacios ni tildes** para Vivado y Vitis (fallan dentro de OneDrive).
8. **Código externo citado:** todo lo que venga del curso, de ayudantías, de internet o de una
   IA se referencia en la cabecera del archivo (campo `Fuente:`) y en el informe.
9. **Git:** no hagas push a `main`. Commit o push solo cuando el usuario lo pida. Mensajes en
   español, como los del historial.

## 3. Arquitectura

```
BoosterPack ──► Pmod ──► PL (design_1) ──AXI GP0──► PS (ARM Cortex-A9, bare-metal)
 S1 S2 JOY_SEL ─ btn_raw[2:0] ─► Debouncer_IP (IP propio) ─ irq_deb ─┐
 joystick, micrófono, acelerómetro ─ ADC SPI ─► axi_quad_spi_1       │
 OPT3001, TMP006 ─ I²C ─► axi_iic_0                                  ├─ xlconcat_0 ─ IRQ_F2P ─► GIC ─► ISR_handler
 DRDY del TMP006 / INT del OPT3001 ─ SENS_INT[7:0] ─► axi_gpio_1 ─ irq_gpio ┤
 axi_timer_0 (1 ms) ─ irq_t0 ; axi_timer_1 (2 ms) ─ irq_t1 ──────────┘
 LCD ◄─ SCLK MOSI CS RS RST ─ axi_quad_spi_0 + axi_gpio_0
 Buzzer ◄─ BUZZER_PWM ◄─ EMIO ◄─ TTC0_WAVE0_OUT (TTC0 del PS)
 microSD ◄─► SDIO0 (PS) ; PC ◄─► UART1 (115200 8N1, COM4)
 vio_0, ila_0 (AC5): sondean btn_raw, EDGE e irq_deb del IP
```

Interrupciones (`IRQ_F2P` → GIC): 61 `axi_timer_0`, 62 `axi_timer_1`, 63 `Debouncer_IP`,
64 `axi_gpio_1`.

| Tick | Qué hace |
|---|---|
| `axi_timer_0`, 1 ms | `sys_ms++`, `song_ms++` si la partida corre (`game_on_off`), descuenta `star_ms`, `buzzer_tick()` |
| `axi_timer_1`, 2 ms | `joy_sample()` (joystick + micrófono) cada tick; acelerómetro y ventana del micrófono cada 5 (10 ms); `frame_flag` cada 15 (30 ms) |
| `irq_deb` | `deb_isr()`: lee `btn_edges` (W1C) → `UI_OK`, `UI_BACK`; JOY_SEL → `g_mute_req` |
| `irq_gpio` | `gpio_isr()`: INT del OPT3001 → `opt_pending = 1`; DRDY del TMP006 → `tmp_drdy = 1` |

Bucle de `main` (`sw/src/main.c`): inicializa todo y luego repite `theme_task()`,
`temp_task()`, `motion_task()`, `audio_user_mute_task()`, la consola de diagnóstico
(`diag_poll()`, solo con `SEP_DIAG`) y, cada 30 ms (`frame_flag`), un paso de la FSM
(`fsm_step(ctx, ev)` con un evento de `ui_pop()` o de la consola) y `render_frame()`.

Máquina de estados (`FsmState` en `interfaces.h`): `ST_INIT → ST_MENU → (ST_LEVEL → ST_PLAY ⇄
ST_PAUSE → ST_GAMEOVER) | ST_RECORDS | ST_CONFIG`. Todo el estado vive en `GameCtx`
(`state_machine.h`, unos 7 KB, `static` en `main`) y se pasa por puntero. El render solo
redibuja todo cuando cambia `ctx->state`, el tema o `redraw_seq`; en el juego repinta solo
lo que cambió (el SPI del LCD es lento).

IP `debouncer_axi` (AXI4-Lite, base `XPAR_DEBOUNCER_IP_BASEADDR`):

| Offset | Registro | Bits |
|---|---|---|
| 0x00 | CTRL | [0] EN, [1] IRQ_EN, [2] INV (botones activos en bajo) |
| 0x04 | DEB_MS | [7:0] antirrebote en ms (reset 20) |
| 0x08 | BTN_MASK | [2:0] bit0 S1, bit1 S2, bit2 JOY_SEL (reset 0x7) |
| 0x0C | STATE | [2:0] nivel filtrado (solo lectura) |
| 0x10 | EDGE | [2:0] flancos pendientes, W1C |
| 0x14 | LED_CTRL | [3:0] LED_PATTERN (reset 0xF), [4] THEME_LIGHT |

## 4. Archivos

### Raíz y carpetas

| Ruta | Contenido |
|---|---|
| `README.md` | presentación del repo, estructura y primeros pasos |
| `CLAUDE.md` | esta guía |
| `docs/MANUAL_IMPLEMENTACION.md` | manual de diseño completo: hardware, IP, firmware, FSM, render, SD, boot, pruebas y riesgos |
| `docs/NOMBRES_DIAGRAMA.md` | correspondencia bloque / señal del diagrama → celda del BD o identificador C |
| `docs/PLAN_TRABAJO.md` | reparto entre Martín (A: placa, periféricos) y el compañero (B: IP, lógica, render) |
| `docs/diagramas/` | `Diagrama_de_bloques_02.drawio` (vigente), PDFs exportados y `SepHero_BD` (viejo, con el mockup) |
| `docs/video/` | material del video |
| `hw/bd/design_1.tcl` | `write_bd_tcl` del block design (lo regenera `02_build_bd.tcl`) |
| `hw/constraints/sephero.xdc` | pines: LCD en JE, ADC en JC, I²C/buzzer/botones en JD, LEDs |
| `hw/constraints/sens_int_candidates.xdc` | los 8 pines del Pmod JA para `SENS_INT[7:0]` |
| `hw/constraints/Zybo-Z7-Master.xdc.ref` | XDC maestro de Digilent, solo referencia |
| `hw/ip_repo/debouncer_axi/` | IP propio empaquetado: `src/debouncer_axi.vhd` (AXI + registros), `src/debouncer_core.vhd` (antirrebote), `src/tb_debouncer_axi.vhd` (testbench), `component.xml`, `xgui/` |
| `hw/export/sephero.xsa`, `design_1_wrapper.ltx` | hardware exportado con bitstream y sondas del ILA/VIO |
| `hw/scripts/00_sim_ip.ps1`, `00_sim_ip_run.tcl` | simulación del testbench del IP con xsim |
| `hw/scripts/01_package_ip.tcl` | empaqueta el IP |
| `hw/scripts/02_build_bd.tcl` | crea el proyecto, arma `design_1`, genera el bitstream y exporta `.xsa` + `.ltx` |
| `hw/scripts/05_vio_ila.tcl` | maneja VIO e ILA en batch (capturas de AC5) |
| `hw/scripts/create_project.tcl` | reconstruye el proyecto Vivado |
| `hw/vivado/` | proyecto Vivado generado (ignorado por git) |
| `sw/src/` | código C de la aplicación (detalle abajo) |
| `sw/lscript.ld` | linker script (stack y heap de 64 KB) |
| `sw/scripts/vitis_platform.tcl` | crea el workspace `sw/workspace` (plataforma con xilffs + app `sep_hero` con `sw/src` enlazada) |
| `sw/scripts/build_app.sh` | compila con el `arm-none-eabi-gcc` de Vitis: `debug` (mudo + diagnóstico), `sound`, `release` |
| `sw/scripts/run_on_board.tcl` | programa la FPGA y corre el ELF por JTAG (revisa JP5 antes) |
| `sw/scripts/make_boot.sh` | genera `boot/BOOT.bin` (FSBL + bit + ELF Release) y opcionalmente lo graba en la QSPI |
| `sw/scripts/uart_session.ps1` | registra la UART y envía teclas programadas a la consola |
| `sw/workspace/` | workspace de Vitis (ignorado por git; hay que crearlo con `vitis_platform.tcl`) |
| `boot/` | `BOOT.bin`, `sep_hero.bif`, `zynq_fsbl.elf`, `sep_hero.elf`, `design_1_wrapper.bit` |
| `sd_card/` | `SONG1.SHC`, `SONG2.SHC` (charts) y `RECORDS.TXT`: van a la raíz de la microSD |
| `tests/` | tests en el PC de la lógica portable: `test_main.c`, `test_logic.c`, `test_fsm.c`, `stubs.c/.h`, `test.h`, `Makefile` (objetivos `run` y `sim`) |
| `sim/` | simulador del LCD en el PC: `fb.c/.h` (framebuffer) y `main_sim.c`; deja `.ppm` en `sim/out/` |
| `tools/make_chart.py` | genera los charts `.SHC` desde melodías escritas en Python |
| `tools/shc2c.py` | convierte `sd_card/*.SHC` en `sw/src/sd_manager_songs.c` (respaldo embebido) |
| `tools/gen_arrows.py` | genera los sprites de flechas (`sw/src/screen_manager_arrows.c/.h`) |
| `tools/ppm2png.py` | convierte los `.ppm` del simulador a PNG |

### `sw/src/` (bloque del diagrama entre paréntesis)

| Archivo | Qué hace |
|---|---|
| `main.c` | inicialización (UART, Debouncer_IP, LCD, ADC, joystick, micrófono, acelerómetro, OPT3001, TMP006, SD, TTC0, GIC) y bucle principal |
| `game_types.h` | tipos y constantes compartidas: `Note`, `Song`, `Game_status`, `Hit_signal`, `Record`, `UiEvent`, `Lane`, `Theme`, parámetros del juego |
| `interfaces.h` | contrato entre módulos: prototipos de todo lo portable, `DebCfg`, `Debouncer`, `UartCfg`, `Buzzer_t`, `FsmState` |
| `hw.h` | prototipos solo de placa y opciones de compilación: `SEP_MUTE`, `OPT_INT_BIT`, `JOY_INVERT_Y`, `MUSIC_START_MUTED`, `TEMP_ALERT_C` |
| `isr_handler.c` (ISR_handler) | GIC, `axi_timer_0/1`, relojes `clock_sys_ms()` / `clock_song_ms()`, `deb_isr()`, `gpio_isr()`, `frame_flag`, `tmp_drdy` |
| `event_queues.c` | colas SPSC ISR → main: golpes (`Hit_signal`) y eventos de interfaz (`UiEvent`) |
| `joystick_aconditioner.c` (Joystick_aconditioner) | ráfaga ADC (X, Y, micrófono); `joy_dir` → golpes en JUEGO o `UI_UP/UI_DOWN` en menús |
| `joystick_aconditioner_logic.c` | detector portable de dirección con eje dominante e histéresis (`joy_detect()`) |
| `microphone_aconditioner.c` (Microphone_aconditioner) | ventanas pico a pico del micrófono → `Crowd_noise` (0..100) |
| `accelerometer_aconditioner.c` (Accelerometer_aconditioner) | calibración, detección de sacudida → `UI_POWER_UP_ENABLE` |
| `light_aconditioner.c` (Light_aconditioner) | OPT3001: comparador de ventana con interrupción, `light_level` (lux), `light_mode_current()` |
| `temperature_aconditioner.c` (Temperature_aconditioner) | TMP006 por I²C, búsqueda del pin DRDY, aviso al llegar a `TEMP_ALERT_C` |
| `state_machine.c/.h` (State_Machine) | FSM, `GameCtx`, `game_on_off()`, nombre del récord, encore, `fsm_apply_cfg()` |
| `game_manager.c` (Game Manager) | juzga golpes (`HIT_WIN_MS`), puntaje, racha, star power, rock meter, fin de partida |
| `sd_manager.c` (SD_Manager) | FatFs sobre SDIO0: monta, carga `SONGn.SHC`, lee y escribe `RECORDS.TXT`; respaldo embebido si falla |
| `sd_manager_formats.c` | parsers portables de `.SHC` y `RECORDS.TXT`, inserción en el top 5 |
| `sd_manager_songs.c` | charts embebidos (generado por `tools/shc2c.py`; no editar a mano) |
| `sound_manager.c` (Sound Manager) | secuenciador del buzzer: canción, jingles y efectos; `buzzer_tick()` cada 1 ms |
| `ttc0_pwm.c` (TTC0 (PWM)) | `buzzer_tone(bz, freq, duty)`, mudo de seguridad y `song_enabler` (JOY_SEL) |
| `screen_manager.c` (Screen Manager) | todas las pantallas: menús, pista de 4 carriles, notas, HUD, GAME OVER, paleta clara/oscura |
| `screen_manager_arrows.c/.h` | sprites de las flechas (generados por `tools/gen_arrows.py`; están en ISO-8859-1) |
| `screen_manager_text.c/.h` | texto con las fuentes del curso |
| `screen_manager_lcd.c` | envío rápido de rectángulos y bitmaps al ST7735S |
| `debouncer_ip.c` | driver del IP: `debouncer_init(Debouncer*, const DebCfg*)`, flancos, LEDs, relectura |
| `debouncer_ip_logic.c` | validación y valores por defecto de `DebCfg` (portable) |
| `uart1.c` (UART1) | `uart_hal_getc()` / `uart_hal_puts()` |
| `uart_config.c` (UART_config) | diálogo no bloqueante del estado CONFIG (`DEB_MS`, `BTN_MASK`, `UMBRAL_SHAKE`, `LED_PATTERN`) y utilidades `fmt_*` |
| `uart_console.c` | consola de diagnóstico (`diag_poll()`, `diag_frame()`, solo con `SEP_DIAG`) |
| `lib/` | drivers del curso: `ADC`, `I2C`, `LCD_*`, `Delay`, fuentes, `platform` (citarlos, no reescribirlos) |

## 5. Compilar, probar y cargar

```bash
# Tests y simulador en el PC (Git Bash; gmake y gcc vienen con Strawberry Perl)
export PATH="/c/Strawberry/c/bin:$PATH"
gmake -C tests run                      # debe terminar en TESTS_OK
gmake -C tests sim                      # pantallas en sim/out/*.ppm (tools/ppm2png.py las pasa a PNG)

# Firmware (requiere crear el workspace una vez; en esta rama aún no está creado)
xsct sw/scripts/vitis_platform.tcl
sh sw/scripts/build_app.sh debug        # buzzer en mudo: lo único que se carga sin el usuario
sh sw/scripts/build_app.sh sound        # con sonido, solo con autorización
xsct sw/scripts/run_on_board.tcl Debug  # JTAG; JP5 en JTAG

# Hardware (solo si cambia el block design; ≤ 2 núcleos)
vivado -mode batch -source hw/scripts/02_build_bd.tcl

# Contenido generado
python tools/make_chart.py && python tools/shc2c.py
python tools/gen_arrows.py
```

Después de un cambio en `sw/src/` corre siempre los tests, el simulador si tocaste el render
y `build_app.sh debug`. Si cambias el hardware, regenera `hw/export/sephero.xsa`, vuelve a
correr `vitis_platform.tcl` y, si corresponde, `make_boot.sh`.

Consola de diagnóstico (build `debug`/`sound`, UART 115200 8N1): `1` OK, `2` atrás, `u`/`j`
cursor, `k` star power, `p` autoplay (`P` fallando 1 de 3), `s` estado, `a` ADC, `o` sensor de
luz, `d` IP, `t` interrupciones, `T` buzzer, `l` patrón del LCD (`x/X/y/Y` offsets), `i`
invierte el eje Y del joystick, `h` ayuda.

## 6. Estado y pendientes conocidos

- El pin del INT del OPT3001 no está confirmado (`OPT_INT_BIT` en `hw.h`). Si no llega, el
  tema se revisa por sondeo.
- El DRDY del TMP006 llega por un cable desde J2.11 de la BoosterPack a JA1.
- No hay microSD a mano: la placa arranca desde la QSPI (`make_boot.sh flash`) y usa los charts
  embebidos.
- Las pruebas físicas (LCD, joystick, botones, sacudida, micrófono, buzzer) las hace el
  usuario con la placa. Tú verificas por UART, releyendo registros, con VIO/ILA, con los tests
  y con el simulador.
