# CLAUDE.md: guía para una IA que trabaje en Sep Hero (rama `avance-p2`)

Este archivo resume lo que una IA necesita saber para trabajar en el repositorio sin romper
nada. Está escrito en español, igual que el código, los comentarios y los commits del
proyecto. Si algo de aquí choca con lo que dice el código, manda el código: avisa y actualiza
este archivo.

## 1. Qué es el proyecto

**Sep Hero** es el Proyecto 2 de IEE2463 Sistemas Electrónicos Programables (PUC, 2026-2). Es
un juego de ritmo estilo DDR / Guitar Hero sobre una **Zybo Z7-10** (Zynq-7010) con la
**BoosterPack EDUMKII** en una tarjeta de extensión Pmod:

- Las notas son **flechas** que caen por 4 carriles (← ↑ ↓ →). Se tocan con las
  **direcciones del joystick** (eje dominante, sin diagonales). Los botones BTN0–3 de la Zybo
  **no se usan**.
- **S1** = OK, **S2** = atrás / pausa, **JOY_SEL** = música ON/OFF (silencio enclavado).
- Sacudir la placa (acelerómetro) activa el *star power* (puntaje ×2) cuando la barra está llena.
- El sensor de luz OPT3001 cambia entre **modo claro y modo oscuro** (LEDs LD0–LD3 y paleta).
- El micrófono mide el público (*encore* en GAME OVER). No está en el avance.
- La melodía suena por el **buzzer** con PWM del TTC0. Las canciones y los récords van en la
  microSD (en el avance: canciones embebidas en el `.elf` y récords en RAM).

Hardware: PL con Vivado 2020.1 (block design `design_1` + IP propio en VHDL). Software:
bare-metal en el ARM Cortex-A9 con Vitis 2020.1 (standalone, C99).

### Ramas

| Rama | Contenido |
|---|---|
| `main` | estructura inicial y reparto de trabajo |
| `ref_proy_2` | versión de referencia anterior (publicada) |
| **`avance-p2`** (esta) | **entrega de avance**: ecosistema reducido, todo por UART + LEDs; el LCD solo muestra la flecha del nivel Demo; el joystick solo genera golpes en PLAY |
| `proyecto-final` | proyecto completo: pantalla de juego, menús con joystick, micrófono, microSD, VIO/ILA. Suele estar en `D:\Sep_martin_pena\proyecto_sep_2` |

Esta rama vive como *worktree* en `D:\Sep_martin_pena\proyecto_sep_2_avance`. No mezclar
cambios entre ramas sin que el usuario lo pida.

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
   actualiza esa tabla. `SepHero_BD.pdf` y `Diagrama_de_bloques_02.pdf` son versiones viejas.
4. **Portabilidad.** `game_manager.c`, `state_machine.c`, `sound_manager.c`,
   `sd_manager_formats.c`, `sd_manager_songs.c`, `debouncer_ip_logic.c`, `uart_config.c`,
   `joystick_aconditioner_logic.c`, `screen_manager.c`, `screen_manager_text.c` y
   `screen_manager_arrows.c` **no incluyen
   headers de Xilinx** ni usan `printf`. Solo llaman funciones de `interfaces.h`. Se compilan en
   el PC con los stubs de `tests/stubs.c`.
5. **Las ISR no usan la UART ni el I²C.** Leen o escriben registros rápidos y dejan banderas
   `volatile` o colas SPSC (`event_queues.c`). El trabajo lento lo hace `main`
   (`temp_task()`, `theme_task()`, `audio_user_mute_task()`).
6. **IP propio:** se edita en `hw/ip_repo/debouncer_axi/src/` y se re-empaqueta con
   `hw/scripts/01_package_ip.tcl` **subiendo la core revision**. Si no, Vivado no lo refresca.
7. **Rutas sin espacios ni tildes** para Vivado y Vitis (fallan dentro de OneDrive).
8. **Código externo citado:** todo lo que venga del curso, de ayudantías, de internet o de una
   IA se referencia en la cabecera del archivo (campo `Fuente:`) y en el informe.
9. **Git:** no hagas push a `main`. Commit o push solo cuando el usuario lo pida. Mensajes en
   español, en imperativo o descriptivos, como los del historial.

## 3. Arquitectura

```
BoosterPack ──► Pmod ──► PL (design_1) ──AXI GP0──► PS (ARM Cortex-A9, bare-metal)
 S1 S2 JOY_SEL ─ btn_raw[2:0] ─► Debouncer_IP (IP propio) ─ irq_deb ─┐
 joystick, acelerómetro ─ ADC SPI ─► axi_quad_spi_1                  │
 OPT3001, TMP006 ─ I²C ─► axi_iic_0                                  ├─ xlconcat_0 ─ IRQ_F2P ─► GIC ─► ISR_handler
 DRDY del TMP006 (cable a JA1) ─ SENS_INT[7:0] ─► axi_gpio_1 ─ irq_gpio ┤
 axi_timer_0 (1 ms) ─ irq_t0 ; axi_timer_1 (2 ms) ─ irq_t1 ──────────┘
 LCD ◄─ SCLK MOSI CS RS RST ─ axi_quad_spi_0 + axi_gpio_0
 Buzzer ◄─ BUZZER_PWM ◄─ EMIO ◄─ TTC0_WAVE0_OUT (TTC0 del PS)
 PC ◄─► UART1 (115200 8N1, COM4)
```

Interrupciones (`IRQ_F2P` → GIC): 61 `axi_timer_0`, 62 `axi_timer_1`, 63 `Debouncer_IP`,
64 `axi_gpio_1`.

| Tick | Qué hace |
|---|---|
| `axi_timer_0`, 1 ms | `sys_ms++`, `song_ms++` si la partida corre (`game_on_off`), descuenta `star_ms`, `buzzer_tick()` |
| `axi_timer_1`, 2 ms | `joy_sample()` cada tick; acelerómetro cada 5 (10 ms); `frame_flag` cada 15 (30 ms); `g_opt_poll` cada 100 (200 ms) |
| `irq_deb` | `deb_isr()`: lee `btn_edges` (W1C) → `UI_OK`, `UI_BACK`; JOY_SEL → `g_mute_req` |
| `irq_gpio` | `gpio_isr()`: DRDY del TMP006 en bajo → `tmp_drdy = 1` |

Bucle de `main` (`sw/src/main.c`): inicializa todo y luego repite `temp_task()`,
`theme_task()`, `motion_task()`, `btn_task()`, `audio_user_mute_task()`, `console_poll()` y,
cada 30 ms (`frame_flag`), un paso de la FSM (`fsm_step(ctx, ev)` con un evento de
`ui_pop()` o de la consola), `render_frame()` y `console_frame()`.

Máquina de estados (`FsmState` en `interfaces.h`): `ST_INIT → ST_MENU → (ST_LEVEL → ST_PLAY ⇄
ST_PAUSE → ST_GAMEOVER) | ST_RECORDS | ST_CONFIG`. En PLAY suena la melodía de la canción;
la partida termina al acabar la canción (victoria) o con el rock meter en 0 (derrota).
Durante los primeros `AVANCE_PLAY_MS` (20 s) la consola imprime `[Jugando] quedan N s`. Todo el estado vive en `GameCtx`
(`state_machine.h`, unos 7 KB, `static` en `main`) y se pasa por puntero.

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
| `docs/AVANCE.md` | qué incluye la entrega de avance, controles y construcción |
| `docs/NOMBRES_DIAGRAMA.md` | correspondencia bloque / señal del diagrama → celda del BD o identificador C |
| `docs/MANUAL_IMPLEMENTACION.md` | manual de diseño completo (del proyecto final; algunas secciones describen lo que el avance no tiene) |
| `docs/PLAN_TRABAJO.md` | reparto entre Martín (A: placa, periféricos) y el compañero (B: IP, lógica, render) |
| `docs/diagramas/` | `Diagrama_de_bloques_02.drawio` (vigente), PDFs exportados y `SepHero_BD` (viejo) |
| `docs/video/` | material del video |
| `hw/bd/design_1.tcl` | `write_bd_tcl` del block design (lo regenera `02_build_bd.tcl`) |
| `hw/constraints/sephero.xdc` | pines: LCD en JE, ADC en JC, I²C/buzzer/botones en JD, LEDs |
| `hw/constraints/sens_int_candidates.xdc` | los 8 pines del Pmod JA para `SENS_INT[7:0]` |
| `hw/constraints/Zybo-Z7-Master.xdc.ref` | XDC maestro de Digilent, solo referencia |
| `hw/ip_repo/debouncer_axi/` | IP propio empaquetado: `src/debouncer_axi.vhd` (AXI + registros), `src/debouncer_core.vhd` (antirrebote), `src/tb_debouncer_axi.vhd` (testbench), `component.xml`, `xgui/` |
| `hw/export/sephero.xsa` | hardware exportado con bitstream (para Vitis sin abrir Vivado) |
| `hw/scripts/00_sim_ip.ps1`, `00_sim_ip_run.tcl` | simulación del testbench del IP con xsim |
| `hw/scripts/01_package_ip.tcl` | empaqueta el IP |
| `hw/scripts/02_build_bd.tcl` | crea el proyecto, arma `design_1`, genera el bitstream y exporta `.xsa` |
| `hw/scripts/05_vio_ila.tcl` | VIO/ILA en batch (solo `proyecto-final`; el avance no tiene VIO/ILA) |
| `hw/scripts/create_project.tcl` | reconstruye el proyecto Vivado |
| `hw/vivado/` | proyecto Vivado generado (ignorado por git) |
| `sw/src/` | código C de la aplicación (detalle abajo) |
| `sw/lscript.ld` | linker script (stack y heap de 64 KB) |
| `sw/scripts/vitis_platform.tcl` | crea el workspace `sw/workspace` (plataforma con xilffs + app `sep_hero` con `sw/src` enlazada) |
| `sw/scripts/build_app.sh` | compila con el `arm-none-eabi-gcc` de Vitis: `debug` (mudo), `sound`, `release`; deja el ELF en `<WS>/<APP>/cli_<cfg>/` (por defecto `sw/workspace/sep_hero`; `WS`, `PLAT` y `APP` apuntan a otro workspace) |
| `sw/scripts/run_on_board.tcl` | programa la FPGA y corre el ELF por JTAG (revisa JP5 antes) |
| `sw/scripts/make_boot.sh` | genera `boot/BOOT.bin` (FSBL + bit + ELF Release) y opcionalmente lo graba en la QSPI |
| `sw/scripts/uart_session.ps1` | registra la UART y envía teclas programadas a la consola |
| `sw/workspace/` | workspace de Vitis (ignorado por git) |
| `boot/` | `BOOT.bin`, `sep_hero.bif`, `zynq_fsbl.elf`, `sep_hero.elf`, `design_1_wrapper.bit` |
| `sd_card/` | `DEMO.SHC`, `SONG1.SHC`, `SONG2.SHC` (charts) y `RECORDS.TXT` |
| `tests/` | tests en el PC de la lógica portable: `test_main.c`, `test_logic.c`, `test_fsm.c`, `stubs.c/.h`, `test.h`, `Makefile` |
| `sim/` | vacío en el avance (el simulador del LCD está en `proyecto-final`) |
| `tools/make_chart.py` | genera los charts `.SHC` desde melodías escritas en Python |
| `tools/shc2c.py` | convierte `sd_card/*.SHC` en `sw/src/sd_manager_songs.c` (respaldo embebido) |

### `sw/src/` (bloque del diagrama entre paréntesis)

| Archivo | Qué hace |
|---|---|
| `main.c` | inicialización (UART, Debouncer_IP, LCD, ADC, joystick, acelerómetro, OPT3001, TMP006, TTC0, GIC) y bucle principal |
| `game_types.h` | tipos y constantes compartidas: `Note`, `Song`, `Game_status`, `Hit_signal`, `Record`, `UiEvent`, `Lane`, `Theme`, parámetros del juego |
| `interfaces.h` | contrato entre módulos: prototipos de todo lo portable, `DebCfg`, `Debouncer`, `UartCfg`, `Buzzer_t`, `FsmState` |
| `hw.h` | prototipos solo de placa (ISR, sensores, LCD, consola) y opciones de compilación: `SEP_MUTE`, `JOY_INVERT_Y`, `MUSIC_START_MUTED`, `TEMP_ALERT_C` |
| `isr_handler.c` (ISR_handler) | GIC, `axi_timer_0/1`, relojes `clock_sys_ms()` / `clock_song_ms()`, `deb_isr()`, `gpio_isr()`, `frame_flag`, `tmp_drdy` |
| `event_queues.c` | colas SPSC ISR → main: golpes (`Hit_signal`) y eventos de interfaz (`UiEvent`) |
| `joystick_aconditioner.c` (Joystick_aconditioner) | calibra el centro; en PLAY encola golpes con sello de tiempo corregido y en los menús envía `UI_UP`/`UI_DOWN` |
| `joystick_aconditioner_logic.c` | detector portable de dirección con eje dominante e histéresis (`joy_detect()`) |
| `accelerometer_aconditioner.c` (Accelerometer_aconditioner) | calibración, detección de sacudida → `UI_POWER_UP_ENABLE` |
| `light_aconditioner.c` (Light_aconditioner) | OPT3001: comparador de ventana, `light_level` (lux), `light_mode_current()` |
| `temperature_aconditioner.c` (Temperature_aconditioner) | TMP006 por I²C, búsqueda del pin DRDY, aviso al llegar a `TEMP_ALERT_C` |
| `state_machine.c/.h` (State_Machine) | FSM, `GameCtx`, `game_on_off()`, textos de cada estado por consola, `fsm_apply_cfg()` |
| `game_manager.c` (Game Manager) | juzga golpes (`HIT_WIN_MS`), puntaje, racha, star power, rock meter, fin de partida |
| `sd_manager.c` (SD_Manager) | FatFs sobre SDIO0: carga `SONGn.SHC` y lee/escribe `RECORDS.TXT`; sin tarjeta usa los charts embebidos y deja los récords en RAM |
| `sd_manager_formats.c` | parsers portables de `.SHC` y `RECORDS.TXT`, inserción en el top 5 |
| `sd_manager_songs.c` | charts embebidos (generado por `tools/shc2c.py`; no editar a mano) |
| `sound_manager.c` (Sound Manager) | secuenciador del buzzer: canción, jingles y efectos; `buzzer_tick()` cada 1 ms |
| `ttc0_pwm.c` (TTC0 (PWM)) | `buzzer_tone(bz, freq, duty)`, mudo de seguridad y `song_enabler` (JOY_SEL) |
| `screen_manager.c` (Screen Manager) | logo, menú y pista de 4 carriles con flechas que caen, receptores, HUD y paleta clara/oscura; repinta solo lo que cambió |
| `screen_manager_arrows.c/.h` | sprites de las flechas (generados por `tools/gen_arrows.py`; están en ISO-8859-1) |
| `screen_manager_text.c/.h` | texto sobre el framebuffer con las fuentes del curso |
| `screen_manager_lcd.c` | envío rápido de rectángulos y bitmaps al ST7735S |
| `debouncer_ip.c` | driver del IP: `debouncer_init(Debouncer*, const DebCfg*)`, flancos, LEDs, relectura |
| `debouncer_ip_logic.c` | validación y valores por defecto de `DebCfg` (portable) |
| `uart1.c` (UART1) | `uart_hal_getc()` / `uart_hal_puts()` |
| `uart_config.c` (UART_config) | diálogo no bloqueante del estado CONFIG (`DEB_MS`, `BTN_MASK`, `UMBRAL_SHAKE`, `LED_PATTERN`) y utilidades `fmt_*` |
| `uart_console.c` | consola de teclado: inyecta eventos, autoplay y volcados de diagnóstico |
| `lib/` | drivers del curso: `ADC`, `I2C`, `LCD_Driver`, `LCD_GUI`, `LCD_SPI`, `Delay`, fuentes, `platform` (citarlos, no reescribirlos) |

## 5. Compilar, probar y cargar

```bash
# Tests de la lógica portable en el PC (Git Bash; gmake y gcc vienen con Strawberry Perl)
export PATH="/c/Strawberry/c/bin:$PATH"
gmake -C tests run                      # debe terminar en TESTS_OK

# Firmware (requiere haber creado el workspace una vez)
xsct sw/scripts/vitis_platform.tcl
sh sw/scripts/build_app.sh debug        # buzzer en mudo: lo único que se carga sin el usuario
sh sw/scripts/build_app.sh sound        # con sonido, solo con autorización
# Contra el workspace de la GUI (app Avance_P2, plataforma AvanceP2):
WS=/c/Users/pdelc/workspace PLAT=AvanceP2 APP=Avance_P2 sh sw/scripts/build_app.sh debug
xsct sw/scripts/run_on_board.tcl Debug  # JTAG; JP5 en JTAG

# Hardware (solo si cambia el block design; ≤ 2 núcleos)
vivado -mode batch -source hw/scripts/02_build_bd.tcl

# Charts
python tools/make_chart.py && python tools/shc2c.py
```

Después de un cambio en `sw/src/` corre siempre los tests y `build_app.sh debug`. Si cambias
el hardware, regenera `hw/export/sephero.xsa`, vuelve a correr `vitis_platform.tcl` y, si
corresponde, `make_boot.sh` (el `BOOT.bin` versionado queda desactualizado si no).

Consola UART (115200 8N1): `1` OK, `2` atrás, `u`/`j` cursor, `k` star power, `p` autoplay,
`s` estado, `o` sensor de luz, `d` IP, `t` interrupciones, `T` buzzer, `h` ayuda.

## 6. Estado y pendientes conocidos

- El INT del OPT3001 no llega a la Zybo: el sensor de luz se revisa cada 200 ms.
- El DRDY del TMP006 llega por un cable desde J2.11 de la BoosterPack a JA1. Si el arranque
  imprime `ninguno` en la búsqueda del DRDY, falta el cable.
- Los menús se manejan por consola y con el joystick (arriba/abajo mueve el cursor).
- Las pruebas físicas (LCD, joystick, botones, sacudida, buzzer) las hace el usuario con la
  placa. Tú verificas por UART, releyendo registros y con los tests.
