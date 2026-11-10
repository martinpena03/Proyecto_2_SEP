# Sep Hero · Manual de implementación (hardware + firmware)

> **Destinatario:** un ingeniero (o un agente de IA) experto en FPGA Xilinx y firmware bare-metal
> que debe implementar Sep Hero completo **sin contexto previo**. El documento es autocontenido:
> fija la arquitectura, los registros, los pines, las interrupciones, los formatos de archivo, la
> máquina de estados y el orden de trabajo, junto con los criterios de aceptación.
>
> **Coherencia:** este manual implementa el **Diagrama de bloques 2**
> (`Diagrama_de_bloques_02.pdf`) y su versión detallada `SepHero_BD.pdf` de 4 páginas (funcional,
> block design, máquina de estados y mockup del LCD), ambos del 2026-11-08 o posteriores. Cuando
> este manual **agrega** algo que los diagramas no tienen (VIO/ILA para AC5, el respaldo sin
> microSD), lo marca con **[+]**. Cuando algo no se puede saber sin la placa, lo marca con
> **[VERIFICAR]** y deja el procedimiento para resolverlo.
>
> ⚠️ **La copia de `docs/diagramas/SepHero_BD.pdf` del repositorio está desactualizada:** es la
> versión anterior, con los BTN0–3 como trastes. Los diagramas vigentes están en la carpeta del
> proyecto en OneDrive (`Proyecto 2 SEP/Diagramas de bloques/`). Hay que copiarlos al repo.

---

## Índice

0. [Reglas para quien implementa](#0-reglas-para-quien-implementa)
1. [Qué es Sep Hero](#1-qué-es-sep-hero)
2. [Plataforma, restricciones y estado actual del hardware](#2-plataforma-restricciones-y-estado-actual-del-hardware)
3. [Arquitectura del sistema](#3-arquitectura-del-sistema)
4. [Hardware en Vivado: block design `design_1`](#4-hardware-en-vivado-block-design-design_1)
5. [IP propio `debouncer_axi` (AO3)](#5-ip-propio-debouncer_axi-ao3)
6. [Depuración en hardware: VIO e ILA (AC5)](#6-depuración-en-hardware-vio-e-ila-ac5)
7. [Firmware en Vitis: plataforma, árbol de fuentes y contratos](#7-firmware-en-vitis-plataforma-árbol-de-fuentes-y-contratos)
8. [Firmware: drivers y módulos de servicio](#8-firmware-drivers-y-módulos-de-servicio)
9. [Firmware: lógica del juego, FSM y render](#9-firmware-lógica-del-juego-fsm-y-render)
10. [Almacenamiento: microSD con respaldo embebido (AC7)](#10-almacenamiento-microsd-con-respaldo-embebido-sd_manager-ac7)
11. [Boot autónomo (AC4)](#11-boot-autónomo-ac4)
12. [Herramientas en el PC: tests, simulador y generadores](#12-herramientas-en-el-pc-tests-simulador-y-generadores)
13. [Plan de implementación por etapas y aceptación](#13-plan-de-implementación-por-etapas-y-aceptación)
14. [Verificación de las 10 actividades](#14-verificación-de-las-10-actividades)
15. [Puntos abiertos, riesgos y solución de problemas](#15-puntos-abiertos-riesgos-y-solución-de-problemas)
16. [Referencias y material base del curso](#16-referencias-y-material-base-del-curso)

---

## 0. Reglas para quien implementa

1. **Fuentes de verdad, en este orden:** (a) este manual; (b) el Diagrama de bloques 2 y
   `SepHero_BD.pdf` vigentes (página 2 para el hardware, páginas 1 y 3 para el software, página 4
   para la pantalla); (c) los `.h` de `sw/src/` (el contrato entre módulos, que §7.4 actualiza);
   (d) el material del curso de §16, que es la **base obligatoria** de los drivers (LCD, ADC, I²C,
   UART). No reescribir desde cero lo que el curso ya entrega funcionando: copiarlo a
   `sw/src/lib/`, citarlo en la cabecera y adaptarlo.
2. **Versiones fijas:** Vivado/Vitis **2020.1** (exigido por el curso) y parte
   **xc7z010clg400-1** (Zybo Z7-10).
3. **No inventar valores de hardware.** Todo pin, polaridad o nombre `XPAR_*` que no esté
   confirmado aquí se marca **[VERIFICAR]**. Se resuelve con el procedimiento indicado y luego se
   actualiza este manual.
4. **Portabilidad:** `game_engine.c`, `game_fsm.c`, `render.c`, `joy_logic.c`, `audio_seq.c`,
   `sd_formats.c`, `debouncer_logic.c` y `uart_config.c` **no incluyen** `xparameters.h` ni
   headers de Xilinx. Se compilan también en el PC con `gcc` (`tests/Makefile`) contra stubs.
5. **Nunca dibujar ni hacer I²C dentro de una ISR.** Las ISR solo leen o escriben registros AXI
   rápidos y dejan banderas o colas. El SPI del ADC es la única excepción (§8.3, §8.4), y está
   acotada.
6. **Comentarios:** la rúbrica evalúa comentarios detallados en todo archivo escrito o modificado
   por el grupo. Cada archivo lleva una cabecera con su autor, su propósito y las fuentes externas
   (curso, ayudantías, internet o IA).
7. **Repositorio:** ramas `feat/<tarea>` y pull request a `main` (ver `README.md`). Al cambiar el
   block design, regenerar `hw/bd/design_1.tcl` y `hw/export/sephero.xsa` (§4.9).

---

## 1. Qué es Sep Hero

Un juego de ritmo que combina guitar hero con **Dance Dance Revolution**. Corre en la Zybo
Z7-10 con la BoosterPack (BOOSTXL-EDUMKII) montada sobre la **tarjeta de extensión del curso**.
Por una pista de **4 carriles** caen **notas con forma de flecha**, de arriba hacia abajo, en la
pantalla LCD de 128×128 de la BoosterPack. En la línea de golpe hay **4 receptores** con el
contorno de las flechas ← ↑ ↓ →. El jugador acierta **apuntando el joystick** en la dirección del
carril cuando la nota pasa sobre su receptor. El buzzer toca la melodía de la canción.

| Mecánica | Implementación |
|---|---|
| Carriles (izq → der) | **← ↑ ↓ →** = `IZQ=0`, `ARRIBA=1`, `ABAJO=2`, `DER=3`, con color fijo: **magenta, verde, amarillo, rojo** |
| Golpe | El joystick pasa de **centro** a una dirección. Cuenta solo el **eje dominante** (sin diagonales). Para repetir la misma flecha hay que volver al centro |
| Acierto | Flecha del joystick = carril de la nota y \|t_golpe − t_nota\| ≤ `HIT_WIN_MS` (120 ms) |
| Fallo | Nota que pasa el receptor sin golpe, o **flecha equivocada** con una nota en la ventana: rock −2 y la racha vuelve a 0 |
| Puntaje | `PTS_NOTA` (100) × multiplicador (×2 en star power) |
| Racha y barra de poder | `STAR_STREAK` (10) aciertos seguidos llenan la barra |
| Star power | Barra llena + **sacudir la placa** (acelerómetro) → 20 s con ×2. Todas las notas y los bordes de la pista se ponen **azules** |
| Rock meter | Acierto +1, fallo −2. Llega a 0 → GAME OVER (derrota) |
| Público | Nivel del **micrófono** en el HUD. Un grito en la pantalla final da el **bonus encore** |
| Tema visual | **Sensor de luz** OPT3001 por interrupción: poca luz → fondo negro; mucha luz → fondo blanco |
| Menús | **Joystick** ↕ para navegar, **S1** (o JOY_SEL) = OK y **S2** = atrás/pausa. Los tres botones pasan por el **IP propio** de antirrebote |
| Configuración | Por **consola UART**: `DEB_MS` y `BTN_MASK` (configuran el IP propio), `JOY_UMBRAL` y `UMBRAL_SHAKE` |
| Canciones y récords | `SONG1.SHC` y `RECORDS.TXT` en la microSD, con respaldo embebido mientras no haya tarjeta |

Los BTN0–BTN3 de la Zybo **no se usan**.

---

## 2. Plataforma, restricciones y estado actual del hardware

### 2.1 Hardware

| Elemento | Detalle |
|---|---|
| Placa | Digilent **Zybo Z7-10**, SoC Zynq-7010 (`xc7z010clg400-1`), ARM Cortex-A9 (se usa **solo CPU0**, sin MicroBlaze) |
| Periféricos de usuario | BoosterPack **BOOSTXL-EDUMKII** conectada mediante la **tarjeta de extensión del curso** a los Pmod **JC**, **JD** y **JE** de la Zybo. La tarjeta de extensión trae el **ADC SPI multicanal** que digitaliza el joystick, el acelerómetro, el micrófono y los 2 potenciómetros de la tarjeta |
| Consola | USB-UART de la Zybo (J12), UART1 del PS (MIO 48–49), **115200 8N1** |
| Boot | Jumper **JP5**: JTAG / QSPI / SD. La Zybo Z7 trae una **QSPI flash de 16 MB** |
| microSD | **No hay tarjeta por ahora** (decisión del usuario: se diseña con SD y con respaldo; ver §10 y §11) |

### 2.2 Software

- Vivado **2020.1** con los *board files* de Digilent instalados (`zybo-z7-10`). Si no están,
  clonar `github.com/Digilent/vivado-boards` y copiar `new/board_files/*` a
  `<Xilinx>/Vivado/2020.1/data/boards/board_files/`.
- Vitis **2020.1** con una aplicación standalone (bare-metal) sobre `ps7_cortexa9_0`.
- PC: `gcc` (MinGW, MSYS2 o WSL) y `make` para los tests; Python 3 para `tools/`.

### 2.3 Estado actual y consecuencias de diseño

**Sin microSD.** Los diagramas ponen el `BOOT.BIN`, la canción y los récords en la SD. Mientras no
haya tarjeta:

1. **AC4 (boot)** se cumple **desde la QSPI flash** (JP5 en QSPI) **[+]**. Cuando haya tarjeta,
   el mismo `BOOT.bin` sirve para bootear desde la SD, como dicen los diagramas (§11).
2. **AC7 (SD)** queda **implementada pero inactiva** **[+]**. El firmware intenta montar la SD;
   si falla, usa la canción **embebida** en el `.elf` (generada desde el mismo `SONG1.SHC`) y
   guarda los récords **en RAM**. Al insertar una tarjeta FAT32 con los archivos, AC7 funciona sin
   recompilar (§10).
3. El PS **igual habilita SD0** (MIO 40–45, CD en MIO 47), así que no hay que volver a Vivado
   cuando llegue la tarjeta.

---

## 3. Arquitectura del sistema

### 3.1 Bloques del Diagrama 2 y su implementación

| Bloque del Diagrama 2 | Región | Implementación |
|---|---|---|
| Btn (S1, S2, JOY_SEL) | periférico | Pmod JD2–JD4 (T15, P14, R14) |
| Joystick, Microphone, Accelerometer | periférico | Analógicos de la Booster → ADC SPI de la tarjeta de extensión |
| Light_Sensor (OPT3001) | periférico | I²C 0x44 + pin INT |
| **Debouncer_IP** | PL | `debouncer_axi_0` (VHDL propio, §5) |
| axi_quad_spi_1 | PL | Maestro SPI del ADC |
| axi_iic_0 | PL | Maestro I²C |
| axi_gpio_1 | PL | 1 canal, 1 bit de entrada (OPT_INT) con interrupción |
| axi_timer_0 · 1 ms / axi_timer_1 · 2 ms | PL | AXI Timer |
| axi_quad_spi_0 + axi_gpio_0 | PL | SPI del LCD + GPIO de DC (RS) y RST |
| EMIO | PL | `TTC0_WAVE0_OUT` → `BUZZER_PWM` |
| GIC | PS | `XScuGic`, `IRQ_F2P` 61–64 |
| ISR_handler | PS | `isr.c` + `input.c` (`deb_isr`, `gpio_isr`) |
| Joystick_aconditioner | PS | `input.c` (`joy_sample`, cada 2 ms) + `joy_logic.c` (portable) |
| Microphone_aconditioner, Accelerometer_aconditioner | PS | `sensors.c` (`sensors_sample`, cada 10 ms) |
| Light_aconditioner | PS | `opt3001.c` + `theme.c` |
| State_Machine | PS | `game_fsm.c` |
| Game Manager | PS | `game_engine.c` |
| Screen Manager | PS | `render.c` + `lcd_fast.c` + `arrows.c` + drivers LCD del curso |
| Sound Manager | PS | `audio_seq.c` (secuenciador) + `audio.c` (`buzzer_tone`) |
| TTC0 (PWM) | PS | TTC0 en modo interval + match |
| SD_Manager | PS | `sd_storage.c` (xilffs) + `sd_formats.c` |
| UART_config + UART1 | PS | `uart_config.c` + `uart_hal.c` |

**Flechas del Diagrama 2 → objetos de C:**

| Flecha | Implementación |
|---|---|
| `btn_raw[2:0]` | Pines → `debouncer_axi_0/btn_in[2:0]` |
| `Btn_edges[2:0] (OK, Back/Pause)` | `deb_isr` lee `EDGE` → `ui_push(UI_OK / UI_BACK)` |
| `joy x, y` / `mic` / `aX, aY, aZ` | `adc_read_raw(ch)` desde `timer1_isr` |
| `Hit{lane, t_ms}` | Cola SPSC de `Hit` (`hit_pop`) |
| `Joy_dir (menú)` | `ui_push(UI_UP / UI_DOWN)` fuera del estado JUEGO |
| `Crowd_noise` | `sensors_crowd_level()` |
| `Power_up_enable` | Evento `UI_SHAKE` → `game_try_star_power()` |
| `Light_mode` | `theme_current()` |
| `song_ms, frame_flag` | `clock_song_ms()`, `g_frame_flag` |
| `Game_on_off` / `Reset` / `Game_over_signal` | `clock_song_run()` + `buzzer_play/stop` / `game_reset()` / retorno de `game_is_over()` |
| `State (enum)` | `ctx->st` (`FsmState`) |
| `Game_status`, `Notas a desplegar` | `const GameState *`, `const Song *` + `now`, entregados a `render_frame()` |
| `Song chart (Note[])`, `Record[5]` | `sd_load_song()`, `sd_load_records()`, `sd_save_records()` |
| `Song_enabler / Select_song` | `buzzer_play(&bz, &song)` / `buzzer_stop()` / selección de archivo |
| `Melodía (freq, dur)` | `Note[]` del chart |
| `freq, duty` | `buzzer_tone(&bz, f_hz, vol)` |
| `DebCfg* → debouncer_init()` | `debouncer_init(&ctx->deb, &cfg)` |
| `lux / CONFIG, LOW/HIGH_LIMIT` | I²C desde `theme_task()` |

### 3.2 Reparto PS/PL

```
                 ┌──────────────────────────── PL (Vivado) ─────────────────────────────┐
 S1,S2,SEL ─────►│ debouncer_axi_0 (IP propio) ──irq──┐                                 │
 OPT_INT ───────►│ axi_gpio_1 (CH1[0]) ─ip2intc_irpt──┤                                 │
 ADC SPI ◄──────►│ axi_quad_spi_1                     ├─► xlconcat_0 ─► IRQ_F2P[3:0] ───┼──► GIC
 LCD SPI ◄───────│ axi_quad_spi_0 + axi_gpio_0 (RS,RST)│                                 │
 I2C OPT3001 ◄──►│ axi_iic_0                          │                                 │
                 │ axi_timer_0 (1 ms) ──interrupt─────┤                                 │
                 │ axi_timer_1 (2 ms) ──interrupt─────┘                                 │
                 │ [+] vio_0, ila_0 (AC5)                                                │
                 └───────── todos esclavos AXI4-Lite de ps7_0_axi_periph (M_AXI_GP0) ─────┘
 BUZZER_PWM ◄──── EMIO ◄── TTC0_WAVE0_OUT (PS)
 UART1 (MIO 48-49), SD0 (MIO 40-47), QSPI (MIO 1-6, 8): directo al PS, sin pasar por la PL
```

### 3.3 Bases de tiempo (modelo de concurrencia)

| Fuente | Periodo | Qué hace | Contexto |
|---|---|---|---|
| `axi_timer_0` → `timer0_isr` | 1 ms | `sys_ms++`; `song_ms++` si la canción corre; `buzzer_tick()`; `star_ms--` | ISR |
| `axi_timer_1` → `timer1_isr` | 2 ms | `joy_sample()` (joystick → golpes o eventos de menú); cada 5 ticks (10 ms) `sensors_sample()` (acelerómetro y micrófono); cada 15 ticks (30 ms, ≈33 FPS) `frame_flag = 1` | ISR |
| `debouncer_axi_0` → `deb_isr` | por evento | Lee `EDGE`, hace W1C y encola `UI_OK` / `UI_BACK` | ISR |
| `axi_gpio_1` → `gpio_isr` | por evento | OPT_INT activo → bandera `opt_pending` | ISR |
| `main()` | bucle | `theme_task()` (I²C), sondeo de la UART y, con `frame_flag`: `fsm_step()` y `render_frame()` | hilo principal |

**Reglas de concurrencia:**

- Toda variable compartida entre ISR y `main` es `volatile`. Las colas (`Hit`, `UiEvent`) son
  anillos **SPSC** (un productor, la ISR; un consumidor, `main`) con tamaño potencia de 2 e índices
  `volatile u8`. No hace falta deshabilitar interrupciones para consumirlas. La cola de UI tiene
  dos productores (`timer1_isr` y `deb_isr`), pero como las ISR no se anidan, sus `ui_push` nunca
  se intercalan.
- No hay anidamiento de interrupciones (comportamiento por defecto del GIC con el handler de
  Xilinx). Toda ISR debe durar < 300 µs. La más larga es `timer1_isr` en los ticks que también
  leen el acelerómetro y el micrófono.
- **El I²C se usa solo desde `main`** (`theme_task`), nunca desde una ISR.
- El **SPI del ADC** (`SpiInstance1`) se usa **solo desde `timer1_isr`**. El **SPI del LCD**
  (`SpiInstance`) se usa **solo desde `main`**. Son controladores distintos y no compiten.
- El tiempo de juego es `song_ms`; el render nunca define el tiempo. Los golpes llevan su propio
  sello de tiempo (resolución de 2 ms por el muestreo del joystick), así que la latencia del
  render no afecta el juicio.

---

## 4. Hardware en Vivado: block design `design_1`

### 4.1 Proyecto

- Parte `xc7z010clg400-1`; `board_part` `digilentinc.com:zybo-z7-10:part0:1.0` (si los board
  files están instalados).
- Block design llamado **`design_1`** (como `SepHero_BD`, página 2), con el wrapper
  `design_1_wrapper` como top.
- **Ajuste del repo:** `hw/scripts/create_project.tcl` hoy referencia `sephero_bd`. Cambiar
  `sephero_bd.tcl` → `design_1.tcl`, `sephero_bd.bd` → `design_1.bd` y `sephero_bd_wrapper` →
  `design_1_wrapper`, y agregar `set_property board_part ... [current_project]` tras
  `create_project`.

### 4.2 `processing_system7_0` (ZYNQ7 PS v5.5)

Partir de **Run Block Automation** con el *preset* de la placa (configura DDR, MIO y relojes de
la Zybo Z7). Después ajustar:

| Opción (GUI) | Valor | Propiedad Tcl (verificar con `report_property`) |
|---|---|---|
| PL Fabric Clocks → FCLK_CLK0 | **50 MHz** | `CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ {50}` |
| PS-PL → AXI GP0 master | habilitado | `CONFIG.PCW_USE_M_AXI_GP0 {1}` |
| Interrupts → Fabric Interrupts → PL-PS **IRQ_F2P** | habilitado | `CONFIG.PCW_USE_FABRIC_INTERRUPT {1}` `CONFIG.PCW_IRQ_F2P_INTR {1}` |
| UART1 | MIO 48..49, 115200 | `CONFIG.PCW_UART1_PERIPHERAL_ENABLE {1}` `CONFIG.PCW_UART1_UART1_IO {MIO 48 .. 49}` `CONFIG.PCW_UART1_BAUD_RATE {115200}` |
| SD0 | MIO 40..45, CD en MIO 47 | `CONFIG.PCW_SD0_PERIPHERAL_ENABLE {1}` `CONFIG.PCW_SD0_SD0_IO {MIO 40 .. 45}` `CONFIG.PCW_SD0_GRP_CD_ENABLE {1}` `CONFIG.PCW_SD0_GRP_CD_IO {MIO 47}` |
| Quad SPI flash | Single SS 4-bit, feedback clk | `CONFIG.PCW_QSPI_PERIPHERAL_ENABLE {1}` `CONFIG.PCW_QSPI_GRP_SINGLE_SS_ENABLE {1}` `CONFIG.PCW_QSPI_GRP_FBCLK_ENABLE {1}` (lo deja el preset; **necesario para el boot desde la QSPI**) |
| Timer 0 (TTC0) | habilitado, **EMIO** | `CONFIG.PCW_TTC0_PERIPHERAL_ENABLE {1}` `CONFIG.PCW_TTC0_TTC0_IO {EMIO}` |
| ENET0, USB0 | opcional: deshabilitar (no se usan) | reduce ruido en el BD y el informe |

Puertos que quedan en el bloque: `DDR`, `FIXED_IO`, `M_AXI_GP0`, `M_AXI_GP0_ACLK`, `FCLK_CLK0`,
`FCLK_RESET0_N`, `IRQ_F2P[3:0]` y `TTC0_WAVE0_OUT` (más `TTC0_WAVE1/2_OUT`, que quedan sin
conectar). `TTC0_WAVE0_OUT` se hace externo como el puerto **`BUZZER_PWM`**.

> El reloj del TTC es CPU_1x, **111,111 MHz** en la Zybo (CPU a 666,67 MHz). El BSP lo expone
> en `xparameters.h` [VERIFICAR el nombre de la macro].

### 4.3 IPs de la PL (configuración exacta)

| Instancia | IP (versión) | Configuración | Puerto AXI |
|---|---|---|---|
| `rst_ps7_0_50M` | Processor System Reset v5.0 | `slowest_sync_clk` = FCLK_CLK0, `ext_reset_in` = FCLK_RESET0_N | — |
| `ps7_0_axi_periph` | AXI Interconnect v2.1 | 1 S → **8 M** (`NUM_MI = 8`) | S00 ← M_AXI_GP0 |
| `axi_quad_spi_0` | AXI Quad SPI v3.2 (LCD) | Standard, Master, `C_NUM_SS_BITS=1`, `C_NUM_TRANSFER_BITS=8`, `C_FIFO_DEPTH=16`, `C_USE_STARTUP=0`, **`C_SCK_RATIO=8`** (6,25 MHz, igual que la referencia del curso a 100 MHz/16; se puede probar 4) | M00 |
| `axi_gpio_0` | AXI GPIO v2.0 | Dual, CH1: 1 bit **salida** (RS/DC), CH2: 1 bit **salida** (RST) | M01 |
| `axi_quad_spi_1` | AXI Quad SPI v3.2 (ADC) | Standard, Master, `C_NUM_SS_BITS=1`, **`C_NUM_TRANSFER_BITS=16`**, **`C_SCK_RATIO=2`**, `C_USE_STARTUP=0` (igual que `Proyecto_Base_Booster` del LAB10). Interfaz `SPI_0` hecha externa | M02 |
| `axi_iic_0` | AXI IIC v2.0 | SCL **100 kHz** (`IIC_FREQ_KHZ=100`). Interfaz `IIC` hecha externa | M03 |
| `axi_gpio_1` | AXI GPIO v2.0 | **1 canal**, `C_GPIO_WIDTH=1`, `C_ALL_INPUTS=1` (OPT_INT), **Enable Interrupt** (`C_INTERRUPT_PRESENT=1`) | M04 |
| `debouncer_axi_0` | `debouncer_axi` v1.0 (propio, §5) | `G_WIDTH = 3`, `G_TICKS_PER_MS = 50000` | M05 |
| `axi_timer_1` | AXI Timer v2.0 (**2 ms**) | Solo el timer 0 del IP (deshabilitar el timer 2: `enable_timer2=0` [VERIFICAR el nombre]), 32 bits | M06 |
| `axi_timer_0` | AXI Timer v2.0 (**1 ms**) | Igual que el anterior | M07 |
| `xlconcat_0` | Concat v2.1 | `NUM_PORTS = 4`, 1 bit cada uno | — |
| **[+]** `vio_0` | VIO v3.0 | ver §6 | — |
| **[+]** `ila_0` | ILA v6.2 | ver §6 | — |

> El orden M00..M07 sigue la página 2 de `SepHero_BD`. No afecta la funcionalidad; se mantiene
> para que el BD coincida con el diagrama.

### 4.4 Conexiones

**Relojes y reset.** `FCLK_CLK0` va a `M_AXI_GP0_ACLK`, a todos los `ACLK`, `S00_ACLK` y
`Mxx_ACLK` del interconnect, a todos los `s_axi_aclk` y `s00_axi_aclk`, a `ext_spi_clk` de ambos
`axi_quad_spi`, a `vio_0/clk` e `ila_0/clk`. `rst_ps7_0_50M/peripheral_aresetn` va a todos los
`*_aresetn` de los esclavos y del interconnect (`interconnect_aresetn` → `ARESETN`).

**Interrupciones** (`xlconcat_0` → `IRQ_F2P`):

| `xlconcat_0` | Fuente | IRQ_F2P | ID del GIC | Handler | Sensibilidad |
|---|---|---|---|---|---|
| In0 | `axi_timer_0/interrupt` (1 ms) | [0] | **61** | `timer0_isr` | nivel alto |
| In1 | `axi_timer_1/interrupt` (2 ms) | [1] | **62** | `timer1_isr` | nivel alto |
| In2 | `debouncer_axi_0/irq` (S1, S2, JOY_SEL) | [2] | **63** | `deb_isr` | nivel alto |
| In3 | `axi_gpio_1/ip2intc_irpt` (OPT_INT) | [3] | **64** | `gpio_isr` | nivel alto |

**Puertos externos del BD → `.xdc`:**

| Puerto del BD | Origen | Pin | Pmod |
|---|---|---|---|
| `SCLK` | `axi_quad_spi_0/sck_o` | V13 | JE7 |
| `MOSI` | `axi_quad_spi_0/io0_o` (ver nota) | J15 | JE3 |
| `CS` | `axi_quad_spi_0/ss_o[0]` | W16 | JE2 |
| `RS_tri_o` | `axi_gpio_0/GPIO` (CH1) | V12 | JE1 |
| `RST_tri_o` | `axi_gpio_0/GPIO2` (CH2) | H15 | JE4 |
| `spi_rtl_io0_io` | `axi_quad_spi_1/SPI_0` (MOSI) | Y14 | JC8 |
| `spi_rtl_io1_io` | `axi_quad_spi_1/SPI_0` (MISO) | W14 | JC7 |
| `spi_rtl_sck_io` | `axi_quad_spi_1/SPI_0` | T12 | JC9 |
| `spi_rtl_ss_io[0]` | `axi_quad_spi_1/SPI_0` | U12 | JC10 |
| `iic_rtl_scl_io` | `axi_iic_0/IIC` | V17 | JD9 |
| `iic_rtl_sda_io` | `axi_iic_0/IIC` | V18 | JD10 |
| `BUZZER_PWM` | `processing_system7_0/TTC0_WAVE0_OUT` | T14 | JD1 |
| `btn_in[0]` (S1) | `debouncer_axi_0/btn_in[0]` | T15 | JD2 |
| `btn_in[1]` (S2) | `debouncer_axi_0/btn_in[1]` | P14 | JD3 |
| `btn_in[2]` (JOY_SEL) | `debouncer_axi_0/btn_in[2]` | R14 | JD4 |
| `opt_int` | `axi_gpio_1/gpio_io_i[0]` | **[VERIFICAR]**, ver §15.1 | par 3 de JD (U14 o U15) |

> **Nota MOSI del LCD [VERIFICAR]:** el diagrama dice `io0_o`, que es el MOSI del AXI Quad SPI
> en modo estándar. El `DEMO.bd` del LAB10 conecta `io1_o` al puerto `MOSI`. Implementar con
> `io0_o`; si el LCD no muestra nada y el resto (RS, RST, SCLK) está bien, probar `io1_o` como en
> la referencia del curso, y dejar anotado cuál funcionó.

### 4.5 Mapa de direcciones

Asignación típica de Vivado (`SepHero_BD`, página 2). **Confirmar en el Address Editor** y usar
siempre las macros `XPAR_*`, nunca literales.

| IP | Base | Rango |
|---|---|---|
| `axi_gpio_0` | 0x4120_0000 | 64 K |
| `axi_gpio_1` | 0x4121_0000 | 64 K |
| `axi_iic_0` | 0x4160_0000 | 64 K |
| `axi_quad_spi_0` | 0x41E0_0000 | 64 K |
| `axi_quad_spi_1` | 0x41E1_0000 | 64 K |
| `axi_timer_0` | 0x4280_0000 | 64 K |
| `axi_timer_1` | 0x4281_0000 | 64 K |
| `debouncer_axi_0` | 0x43C0_0000 | 64 K |

### 4.6 Constraints (`hw/constraints/sephero.xdc`)

Basado en `Zybo-Z7-Master.xdc` y en los `.xdc` de la AYU10, la AYU11 y el LAB10. Los nombres de
puerto deben coincidir con los del wrapper.

```tcl
## ---- LCD (Pmod JE) - igual que LAB10/AYU11
set_property -dict { PACKAGE_PIN V12 IOSTANDARD LVCMOS33 } [get_ports { RS_tri_o }]
set_property -dict { PACKAGE_PIN W16 IOSTANDARD LVCMOS33 } [get_ports { CS }]
set_property -dict { PACKAGE_PIN J15 IOSTANDARD LVCMOS33 } [get_ports { MOSI }]
set_property -dict { PACKAGE_PIN H15 IOSTANDARD LVCMOS33 } [get_ports { RST_tri_o }]
set_property -dict { PACKAGE_PIN V13 IOSTANDARD LVCMOS33 } [get_ports { SCLK }]

## ---- ADC SPI de la tarjeta de extensión (Pmod JC) - igual que LAB10
set_property -dict { PACKAGE_PIN W14 IOSTANDARD LVCMOS33 } [get_ports { spi_rtl_io1_io }]
set_property -dict { PACKAGE_PIN Y14 IOSTANDARD LVCMOS33 } [get_ports { spi_rtl_io0_io }]
set_property -dict { PACKAGE_PIN T12 IOSTANDARD LVCMOS33 } [get_ports { spi_rtl_sck_io }]
set_property -dict { PACKAGE_PIN U12 IOSTANDARD LVCMOS33 } [get_ports { spi_rtl_ss_io[0] }]

## ---- I2C OPT3001 (Pmod JD) - igual que AYU10
set_property -dict { PACKAGE_PIN V17 IOSTANDARD LVCMOS33 } [get_ports { iic_rtl_scl_io }]
set_property -dict { PACKAGE_PIN V18 IOSTANDARD LVCMOS33 } [get_ports { iic_rtl_sda_io }]

## ---- Buzzer (TTC0_WAVE0_OUT por EMIO) - AYU10
set_property -dict { PACKAGE_PIN T14 IOSTANDARD LVCMOS33 } [get_ports { BUZZER_PWM }]

## ---- Botones de la Booster -> debouncer_axi_0/btn_in (Pmod JD) - AYU10 (BTN0, BTN1, BTN_JY)
set_property -dict { PACKAGE_PIN T15 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { btn_in[0] }]
set_property -dict { PACKAGE_PIN P14 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { btn_in[1] }]
set_property -dict { PACKAGE_PIN R14 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { btn_in[2] }]

## ---- INT del OPT3001 (open-drain, activo en bajo) -> axi_gpio_1 CH1[0]
## [VERIFICAR] pin: par 3 de JD (U14 = jd_p[3], U15 = jd_n[3]); ver §15.1.
set_property -dict { PACKAGE_PIN U14 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { opt_int[0] }]
```

> Si el BD exporta un nombre distinto (p. ej. `TTC0_WAVE0_OUT_0`), usar el nombre real del
> wrapper. Los `PULLUP true` de S1, S2, SEL y OPT_INT no estorban aunque la Booster ya tenga
> resistencias.

### 4.7 Timing

Todo corre a 50 MHz y no hay cruces de dominio, salvo las entradas asíncronas de los botones,
que se sincronizan dentro del IP con 2 FF `ASYNC_REG`. Exigir **WNS ≥ 0** después de la
implementación. VIO e ILA agregan su propio *debug hub*; no requieren constraints extra.

### 4.8 Validación del BD

`Validate Design` sin errores. Revisar que (1) `IRQ_F2P` tenga ancho 4, (2) el `irq` del IP
propio aparezca como interfaz de interrupción (si no, el BSP no genera
`XPAR_FABRIC_DEBOUNCER_AXI_0_IRQ_INTR`; ver §5.5) y (3) no queden bloques ni conexiones sin uso
(la rúbrica descuenta los bloques "basura").

### 4.9 Generación y exportación

```tcl
# Desde la Tcl Console de Vivado, con el proyecto abierto:
validate_bd_design
save_bd_design
make_wrapper -files [get_files design_1.bd] -top -import
launch_runs impl_1 -to_step write_bitstream -jobs 2   ;# nunca mas de 2 nucleos
wait_on_run impl_1
write_hw_platform -fixed -include_bit -force hw/export/sephero.xsa
# Guardar el BD en el repo (regenerable con create_project.tcl):
write_bd_tcl -force -include_layout hw/bd/design_1.tcl
```

Subir a `hw/export/` también el `.ltx` (`design_1_wrapper.ltx`), que contiene las probes de VIO
e ILA.

---

## 5. IP propio `debouncer_axi` (AO3)

### 5.1 Función

Antirrebote de **3 botones de la Booster** (S1 = OK, S2 = atrás/pausa, JOY_SEL = OK) con tiempo
configurable en ejecución (`DEB_MS`), detector de **flanco de presión** enmascarado con
`BTN_MASK`, latch de flancos `EDGE` con borrado por escritura de 1 (W1C) e interrupción `irq` de
nivel. El procesador lo configura por AXI4-Lite con `debouncer_init(&deb, &cfg)`, usando
parámetros que el usuario escribe en la consola UART. Así cumple AO3: habla con un periférico
(los botones), con el procesador (AXI + IRQ) y se inicializa con una función que recibe
punteros.

Se basa en el `debouncer.vhd` del Proyecto 1 del grupo (`P1_FInal/debouncer`, contador de
estabilidad por botón) con `WIDTH = 3`. Lo nuevo de este proyecto: el umbral se lee de un registro
con un *tick* de 1 ms (equivale a `STABLE = DEB_MS × 50 000 ciclos` del diagrama); además se
agregan la polaridad configurable, el detector de flanco, la máscara, el latch W1C, la IRQ y la
interfaz AXI4-Lite propia.

### 5.2 Puertos

| Puerto | Dir | Ancho | Descripción |
|---|---|---|---|
| `btn_in` | in | 3 | `[0]` S1, `[1]` S2, `[2]` JOY_SEL (asíncronos; en la Booster son activos en bajo [VERIFICAR]) |
| `btn_virt` | in | 3 | **[+]** Botones virtuales desde el VIO (AC5), activos en alto; se combinan con OR **después** de la corrección de polaridad. Default `"000"` |
| `irq` | out | 1 | Nivel alto mientras `EDGE ≠ 0` y `CTRL.IRQ_EN = 1` |
| `dbg_state` | out | 3 | **[+]** Copia de `STATE` para el ILA y el VIO |
| `dbg_edge` | out | 3 | **[+]** Copia de `EDGE` para el ILA y el VIO |
| `dbg_tick` | out | 1 | **[+]** Pulso de 1 ms (calificador de captura del ILA) |
| `s00_axi_*` | — | — | Esclavo AXI4-Lite de 32 bits de datos y 5 bits de dirección |

### 5.3 Mapa de registros (offset desde `XPAR_DEBOUNCER_AXI_0_S00_AXI_BASEADDR`)

| Offset | Nombre | Acceso | Bits | Reset | Descripción |
|---|---|---|---|---|---|
| 0x00 | `CTRL` | R/W | [0] `EN`, [1] `IRQ_EN`, [2] `INV` **[+]** | 0 | `EN = 0` pone `STATE` en 0 y detiene los flancos. `INV = 1` invierte las entradas (botones activos en bajo), así la polaridad se corrige por software sin re-sintetizar |
| 0x04 | `DEB_MS` | R/W | [7:0] | 20 | Tiempo de antirrebote en ms (1..255; 0 se trata como 1) |
| 0x08 | `BTN_MASK` | R/W | [2:0] | 0x7 | Botones habilitados: bit0 S1, bit1 S2, bit2 JOY_SEL |
| 0x0C | `STATE` | R | [2:0] | 0 | Nivel filtrado ("presionado" = 1) |
| 0x10 | `EDGE` | R / **W1C** | [2:0] | 0 | Flancos de presión pendientes (ya enmascarados). Escribir 1 en el bit i lo borra |

Semántica: `STATE(i)` toma el valor de la entrada sincronizada (con la polaridad corregida)
cuando esta **difiere** de `STATE(i)` durante `DEB_MS` *ticks* de 1 ms **seguidos** (cualquier
rebote reinicia la cuenta). El retardo de filtrado queda entre `DEB_MS−1` y `DEB_MS` ms. `EDGE(i)`
se activa en el ciclo en que `STATE(i)` pasa de 0 a 1 si `BTN_MASK(i) = 1`. Si un flanco nuevo
coincide con un W1C del mismo bit, gana el flanco (no se pierden pulsaciones).

### 5.4 VHDL

Archivos en `hw/ip_repo/debouncer_axi/src/`: `debouncer_core.vhd`, `debouncer_axi.vhd` y
`tb_debouncer_axi.vhd`. VHDL-93 sintetizable.

#### 5.4.1 `debouncer_core.vhd`

```vhdl
-- debouncer_core.vhd - Antirrebote de N botones con tiempo configurable en runtime.
-- Sep Hero (IEE2463 Proyecto 2). Basado en debouncer.vhd del Proyecto 1 del grupo
-- (contador de estabilidad por botón); se reemplaza el genérico STABLE_TIME por la
-- entrada deb_ms y un tick de 1 ms, y se agrega el detector de flanco de subida.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity debouncer_core is
  generic (
    G_WIDTH        : positive := 3;
    G_TICKS_PER_MS : positive := 50_000      -- ciclos de reloj por ms (FCLK_CLK0 = 50 MHz)
  );
  port (
    clk     : in  std_logic;
    rst_n   : in  std_logic;                 -- reset síncrono activo en bajo
    en      : in  std_logic;                 -- CTRL.EN
    deb_ms  : in  unsigned(7 downto 0);      -- DEB_MS
    btn_in  : in  std_logic_vector(G_WIDTH-1 downto 0);  -- ya en lógica positiva
    state   : out std_logic_vector(G_WIDTH-1 downto 0);  -- nivel filtrado
    rise    : out std_logic_vector(G_WIDTH-1 downto 0);  -- pulso de 1 ciclo por flanco 0->1
    tick_ms : out std_logic                  -- pulso de 1 ciclo cada 1 ms
  );
end entity;

architecture rtl of debouncer_core is
  signal presc        : natural range 0 to G_TICKS_PER_MS-1 := 0;
  signal tick         : std_logic := '0';
  signal sync0, sync1 : std_logic_vector(G_WIDTH-1 downto 0) := (others => '0');
  signal st, st_d     : std_logic_vector(G_WIDTH-1 downto 0) := (others => '0');
  type cnt_t is array (0 to G_WIDTH-1) of unsigned(7 downto 0);
  signal cnt          : cnt_t := (others => (others => '0'));
  attribute ASYNC_REG : string;
  attribute ASYNC_REG of sync0 : signal is "TRUE";
  attribute ASYNC_REG of sync1 : signal is "TRUE";
begin
  -- Prescaler: un pulso 'tick' por milisegundo.
  p_tick : process(clk)
  begin
    if rising_edge(clk) then
      if rst_n = '0' then
        presc <= 0;  tick <= '0';
      elsif presc = G_TICKS_PER_MS-1 then
        presc <= 0;  tick <= '1';
      else
        presc <= presc + 1;  tick <= '0';
      end if;
    end if;
  end process;

  -- Sincronizador de 2 FF + contador de estabilidad por botón.
  p_deb : process(clk)
  begin
    if rising_edge(clk) then
      sync0 <= btn_in;
      sync1 <= sync0;
      if rst_n = '0' or en = '0' then
        st   <= (others => '0');
        st_d <= (others => '0');
        cnt  <= (others => (others => '0'));
      else
        st_d <= st;
        for i in 0 to G_WIDTH-1 loop
          if sync1(i) = st(i) then
            cnt(i) <= (others => '0');             -- entrada igual al estado: nada que filtrar
          elsif tick = '1' then
            if cnt(i) + 1 >= deb_ms then           -- estable DEB_MS ms seguidos
              st(i)  <= sync1(i);
              cnt(i) <= (others => '0');
            else
              cnt(i) <= cnt(i) + 1;
            end if;
          end if;
        end loop;
      end if;
    end if;
  end process;

  state   <= st;
  rise    <= st and not st_d;
  tick_ms <= tick;
end architecture;
```

#### 5.4.2 `debouncer_axi.vhd` (top con esclavo AXI4-Lite)

Esclavo AXI4-Lite escrito a mano: compacto, sin el banco genérico de la plantilla. Acepta una
escritura cuando `AWVALID` y `WVALID` están ambos en alto (permitido por AXI). `BVALID` sube
**después** del handshake, como exige el protocolo. Los puertos usan el prefijo `s00_axi_` para
que el empaquetador infiera la interfaz automáticamente.

```vhdl
-- debouncer_axi.vhd - IP propio de Sep Hero: debouncer_core + registros AXI4-Lite + irq.
-- Botones: [0] S1, [1] S2, [2] JOY_SEL de la BoosterPack.
-- Mapa: 0x00 CTRL, 0x04 DEB_MS, 0x08 BTN_MASK, 0x0C STATE (R), 0x10 EDGE (R/W1C).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity debouncer_axi is
  generic (
    C_S00_AXI_DATA_WIDTH : integer  := 32;
    C_S00_AXI_ADDR_WIDTH : integer  := 5;
    G_WIDTH              : positive := 3;
    G_TICKS_PER_MS       : positive := 50_000
  );
  port (
    btn_in          : in  std_logic_vector(G_WIDTH-1 downto 0);
    btn_virt        : in  std_logic_vector(G_WIDTH-1 downto 0) := (others => '0');
    irq             : out std_logic;
    dbg_state       : out std_logic_vector(G_WIDTH-1 downto 0);
    dbg_edge        : out std_logic_vector(G_WIDTH-1 downto 0);
    dbg_tick        : out std_logic;
    s00_axi_aclk    : in  std_logic;
    s00_axi_aresetn : in  std_logic;
    s00_axi_awaddr  : in  std_logic_vector(C_S00_AXI_ADDR_WIDTH-1 downto 0);
    s00_axi_awprot  : in  std_logic_vector(2 downto 0);
    s00_axi_awvalid : in  std_logic;
    s00_axi_awready : out std_logic;
    s00_axi_wdata   : in  std_logic_vector(C_S00_AXI_DATA_WIDTH-1 downto 0);
    s00_axi_wstrb   : in  std_logic_vector((C_S00_AXI_DATA_WIDTH/8)-1 downto 0);
    s00_axi_wvalid  : in  std_logic;
    s00_axi_wready  : out std_logic;
    s00_axi_bresp   : out std_logic_vector(1 downto 0);
    s00_axi_bvalid  : out std_logic;
    s00_axi_bready  : in  std_logic;
    s00_axi_araddr  : in  std_logic_vector(C_S00_AXI_ADDR_WIDTH-1 downto 0);
    s00_axi_arprot  : in  std_logic_vector(2 downto 0);
    s00_axi_arvalid : in  std_logic;
    s00_axi_arready : out std_logic;
    s00_axi_rdata   : out std_logic_vector(C_S00_AXI_DATA_WIDTH-1 downto 0);
    s00_axi_rresp   : out std_logic_vector(1 downto 0);
    s00_axi_rvalid  : out std_logic;
    s00_axi_rready  : in  std_logic
  );
end entity;

architecture rtl of debouncer_axi is
  constant W   : positive := G_WIDTH;
  constant ZW  : std_logic_vector(W-1 downto 0) := (others => '0');
  signal clk, rstn       : std_logic;
  signal awready, wready : std_logic := '0';
  signal bvalid          : std_logic := '0';
  signal arready, rvalid : std_logic := '0';
  signal rdata           : std_logic_vector(31 downto 0) := (others => '0');
  -- registros
  signal ctrl     : std_logic_vector(2 downto 0) := "000";      -- INV, IRQ_EN, EN
  signal deb_ms   : unsigned(7 downto 0) := to_unsigned(20, 8);
  signal btn_mask : std_logic_vector(W-1 downto 0) := (others => '1');
  signal edge     : std_logic_vector(W-1 downto 0) := (others => '0');
  signal w1c      : std_logic_vector(W-1 downto 0) := (others => '0');
  signal irq_r    : std_logic := '0';
  -- núcleo
  signal btn_pos, btn_mix, state, rise : std_logic_vector(W-1 downto 0);
begin
  clk     <= s00_axi_aclk;
  rstn    <= s00_axi_aresetn;
  -- Polaridad (CTRL.INV) y botones virtuales del VIO (AC5), ambos antes del sincronizador.
  btn_pos <= not btn_in when ctrl(2) = '1' else btn_in;
  btn_mix <= btn_pos or btn_virt;

  u_core : entity work.debouncer_core
    generic map (G_WIDTH => W, G_TICKS_PER_MS => G_TICKS_PER_MS)
    port map (clk => clk, rst_n => rstn, en => ctrl(0), deb_ms => deb_ms,
              btn_in => btn_mix, state => state, rise => rise, tick_ms => dbg_tick);

  -- Canal de escritura: AW+W simultáneos -> 1 ciclo de READY -> escribe -> BVALID.
  p_wr : process(clk)
  begin
    if rising_edge(clk) then
      if rstn = '0' then
        awready <= '0';  wready <= '0';  bvalid <= '0';
        ctrl <= "000";  deb_ms <= to_unsigned(20, 8);  btn_mask <= (others => '1');
        w1c  <= (others => '0');
      else
        w1c <= (others => '0');
        if awready = '0' and s00_axi_awvalid = '1' and s00_axi_wvalid = '1' and bvalid = '0' then
          awready <= '1';  wready <= '1';
        else
          awready <= '0';  wready <= '0';
        end if;
        if awready = '1' and s00_axi_awvalid = '1' and s00_axi_wvalid = '1' then
          if s00_axi_wstrb(0) = '1' then
            case s00_axi_awaddr(4 downto 2) is
              when "000"  => ctrl     <= s00_axi_wdata(2 downto 0);
              when "001"  => deb_ms   <= unsigned(s00_axi_wdata(7 downto 0));
              when "010"  => btn_mask <= s00_axi_wdata(W-1 downto 0);
              when "100"  => w1c      <= s00_axi_wdata(W-1 downto 0);
              when others => null;                       -- STATE es solo lectura
            end case;
          end if;
          bvalid <= '1';
        elsif bvalid = '1' and s00_axi_bready = '1' then
          bvalid <= '0';
        end if;
      end if;
    end if;
  end process;

  -- Latch de flancos (W1C; si un flanco coincide con el borrado, gana el flanco) e irq.
  p_edge : process(clk)
  begin
    if rising_edge(clk) then
      if rstn = '0' then
        edge <= (others => '0');  irq_r <= '0';
      else
        edge <= (edge and not w1c) or (rise and btn_mask);
        if edge /= ZW and ctrl(1) = '1' then irq_r <= '1'; else irq_r <= '0'; end if;
      end if;
    end if;
  end process;

  -- Canal de lectura.
  p_rd : process(clk)
  begin
    if rising_edge(clk) then
      if rstn = '0' then
        arready <= '0';  rvalid <= '0';  rdata <= (others => '0');
      else
        if arready = '0' and s00_axi_arvalid = '1' and rvalid = '0' then
          arready <= '1';
        else
          arready <= '0';
        end if;
        if arready = '1' and s00_axi_arvalid = '1' then
          rvalid <= '1';
          rdata  <= (others => '0');
          case s00_axi_araddr(4 downto 2) is
            when "000"  => rdata(2 downto 0)   <= ctrl;
            when "001"  => rdata(7 downto 0)   <= std_logic_vector(deb_ms);
            when "010"  => rdata(W-1 downto 0) <= btn_mask;
            when "011"  => rdata(W-1 downto 0) <= state;
            when "100"  => rdata(W-1 downto 0) <= edge;
            when others => null;
          end case;
        elsif rvalid = '1' and s00_axi_rready = '1' then
          rvalid <= '0';
        end if;
      end if;
    end if;
  end process;

  s00_axi_awready <= awready;
  s00_axi_wready  <= wready;
  s00_axi_bresp   <= "00";
  s00_axi_bvalid  <= bvalid;
  s00_axi_arready <= arready;
  s00_axi_rdata   <= rdata;
  s00_axi_rresp   <= "00";
  s00_axi_rvalid  <= rvalid;
  irq       <= irq_r;
  dbg_state <= state;
  dbg_edge  <= edge;
end architecture;
```

### 5.5 Empaquetado como IP

1. `Tools → Create and Package New IP → Package a specified directory`, apuntando a
   `hw/ip_repo/debouncer_axi/` (con `src/` adentro). También sirve el flujo de la AYU05
   ("Create a new AXI4 peripheral", 8 registros) reemplazando los VHDL generados por estos dos
   archivos (con los mismos nombres de puerto).
2. **Identification:** vendor `uc.cl`, library `sephero`, name `debouncer_axi`, version 1.0.
3. **Ports and Interfaces:** verificar que se infiera `S00_AXI` (aximm, slave) asociada a
   `s00_axi_aclk` (con `ASSOCIATED_BUSIF S00_AXI` y `ASSOCIATED_RESET s00_axi_aresetn`). Sobre
   `irq`: *Add Bus Interface* `xilinx.com:signal:interrupt:1.0`, modo master, parámetro
   **`SENSITIVITY = LEVEL_HIGH`**. Sin esto, el BSP no genera el `XPAR_FABRIC_..._IRQ_INTR` del IP.
4. **Addressing and Memory:** espacio de 32 B mínimo (Vivado lo redondea a 64 K en el
   Address Editor).
5. **Customization Parameters:** exponer `G_WIDTH` (default 3) y `G_TICKS_PER_MS` (default 50000).
6. **Review and Package:** **subir la core revision en cada re-empaquetado.** Luego, en el
   proyecto principal: `IP Catalog → Refresh`, `Report IP Status → Upgrade Selected`.

### 5.6 Testbench (`tb_debouncer_axi.vhd`)

Simulación en xsim con `G_TICKS_PER_MS => 10` (1 "ms" = 10 ciclos de 20 ns). Los
procedimientos AXI se declaran dentro del proceso de estímulo, así manejan directamente las
señales de la arquitectura.

```vhdl
-- Fragmento: procedimientos de bus dentro del proceso 'stim'
stim : process
  variable rd : std_logic_vector(31 downto 0);
  procedure axi_write(addr : natural; data : std_logic_vector(31 downto 0)) is
  begin
    awaddr <= std_logic_vector(to_unsigned(addr, 5));  wdata <= data;  wstrb <= "1111";
    awvalid <= '1';  wvalid <= '1';  bready <= '1';
    wait until rising_edge(clk) and awready = '1';
    awvalid <= '0';  wvalid <= '0';
    wait until rising_edge(clk) and bvalid = '1';
    bready <= '0';
  end procedure;
  procedure axi_read(addr : natural; data : out std_logic_vector(31 downto 0)) is
  begin
    araddr <= std_logic_vector(to_unsigned(addr, 5));  arvalid <= '1';  rready <= '1';
    wait until rising_edge(clk) and arready = '1';
    arvalid <= '0';
    wait until rising_edge(clk) and rvalid = '1';
    data := rdata;  rready <= '0';
  end procedure;
begin
  ...
```

Casos obligatorios, todos con `assert ... severity error`. Los botones se modelan activos en
bajo (reposo = '1') y `INV = 1`:

| # | Estímulo | Esperado |
|---|---|---|
| T1 | Reset y lectura de los 5 registros | CTRL=0, DEB_MS=20, BTN_MASK=0x7, STATE=0, EDGE=0 |
| T2 | CTRL=0b111, DEB_MS=5; `btn_in(0)` baja con rebotes de 1–3 "ms" y luego queda estable 8 "ms" | Una sola transición de STATE(0); EDGE=0x1; `irq`=1 |
| T3 | W1C: escribir 0x1 en EDGE | EDGE=0; `irq` baja como máximo 3 ciclos después |
| T4 | BTN_MASK=0x6 y pulsación de S1 | STATE(0)=1 pero EDGE(0)=0 y sin `irq` |
| T5 | CTRL.IRQ_EN=0 con una pulsación | EDGE se activa, `irq`=0 |
| T6 | CTRL.EN=0 | STATE=0 y sin flancos |
| T7 | Flanco nuevo en el mismo ciclo que el W1C de ese bit | EDGE queda en 1 |
| T8 | `btn_virt(1)` = 1 con los botones en reposo | Igual que presionar S2 |
| T9 | Glitch más corto que DEB_MS | Sin cambio de STATE |
| T10 | CTRL.INV=0 con los botones en reposo ('1') | STATE pasa a 0b111 tras DEB_MS (comprueba la polaridad) |

Guardar capturas de la forma de onda (T2 y T3) para el informe.

---

## 6. Depuración en hardware: VIO e ILA (AC5)

**[+] No están en los diagramas** (el usuario pidió dejarlos fuera por ahora; la tabla de
actividades los marca como pendientes). Son obligatorios para AC5 ("interactuar con el módulo y
validar las transacciones") y siguen la AY04 (VIO + ILA con trigger condicionado). Se conectan
al IP propio. Al agregarlos al BD, actualizar los diagramas.

### 6.1 Instancias

| Instancia | Configuración | Conexión |
|---|---|---|
| `vio_0` (VIO v3.0) | `C_NUM_PROBE_IN=2` (3 bits cada una), `C_NUM_PROBE_OUT=1` (3 bits, valor inicial 0) | `clk`=FCLK_CLK0; `probe_in0` ← `debouncer_axi_0/dbg_state`; `probe_in1` ← `dbg_edge`; `probe_out0` → `debouncer_axi_0/btn_virt` |
| `ila_0` (ILA v6.2) | Native, 5 probes: P0 `btn_in`(3), P1 `dbg_state`(3), P2 `dbg_edge`(3), P3 `irq`(1), P4 `dbg_tick`(1); `C_DATA_DEPTH=4096`; **Capture Control habilitado** (`C_EN_STRG_QUAL=1`); 2 comparadores por probe | `clk`=FCLK_CLK0; P0 se conecta a la misma red del puerto externo `btn_in` |

Opcional: un segundo slot del ILA en modo **AXI monitor** sobre la red `M05_AXI` (como el ILA
de la AYU05) para ver la transacción AXI de `debouncer_init` (`AWADDR`, `WDATA`, `BRESP`).

### 6.2 Experimentos para el informe y el video

1. **Rebote real (ILA, captura ciclo a ciclo):** trigger `btn_in[0]` = F (flanco de bajada, S1
   activo en bajo), posición del trigger en 10 %. Con 4096 muestras a 50 MHz la ventana es de
   82 µs: se ven los rebotes de S1 en `btn_in` mientras `dbg_state` sigue estable.
2. **Antirrebote completo (ILA con capture control):** condición de captura `dbg_tick == 1`, es
   decir, una muestra por ms y una ventana de 4,1 s. Trigger `dbg_edge != 0`. Se ve `btn_in`
   rebotando, `dbg_state` subiendo `DEB_MS` ms después, `dbg_edge` en 1 e `irq` en 1 hasta que la
   CPU hace el W1C (bajada de `irq` < 1 ms después). Repetir con `DEB_MS` = 5 y 40 configurados
   por UART y mostrar que el retardo cambia: valida la transacción PS → IP.
3. **VIO como estímulo:** en el estado JUEGO, poner `probe_out0` = 0b010 (S2 virtual) y volver a
   0: el juego entra en PAUSA. En el menú, `0b001` (S1 virtual) confirma la opción.
   `probe_in0/1` muestran `STATE` y `EDGE` en vivo. Con `BTN_MASK` = 0x5 (S2 deshabilitado) el
   mismo estímulo no genera `EDGE`: valida que la máscara escrita desde la UART llegó al hardware.

### 6.3 VIO vs ILA (explicitar en el informe)

| | VIO | ILA |
|---|---|---|
| Qué es | Entradas y salidas virtuales controladas desde el Hardware Manager | Analizador lógico integrado que guarda muestras en BRAM |
| Dirección | Lee señales (probe_in) **y las maneja** (probe_out) | Solo observa |
| Temporalidad | Valores "en vivo", refresco de ~250 ms; no sirve para eventos rápidos | Captura ciclo a ciclo alrededor de un trigger, con profundidad finita |
| Uso aquí | Inyectar pulsaciones de S1/S2/SEL y leer STATE/EDGE | Ver los rebotes, el retardo de filtrado y la IRQ |
| Costo | Pocos LUT/FF | BRAM proporcional a ancho × profundidad |

Los cores de debug quedan en el bitstream final: están justificados por AC5 y con
`btn_virt = 0` no afectan el funcionamiento.

---

## 7. Firmware en Vitis: plataforma, árbol de fuentes y contratos

### 7.1 Plataforma

1. `File → New → Platform Project` desde `hw/export/sephero.xsa`; OS **standalone**; CPU
   `ps7_cortexa9_0`; marcar **Generate boot components** (crea `zynq_fsbl`).
2. BSP → *Board Support Package Settings*:
   - `stdin`/`stdout` = `ps7_uart_1`.
   - Librería **`xilffs`**: `fs_interface = 1` (SD), `read_only = false`, `use_lfn = 0` (nombres
     8.3: `SONG1.SHC` y `RECORDS.TXT` caben).
3. Aplicación: `Empty Application (C)` con el nombre `sep_hero`. Agregar `sw/src/` como
   **carpeta enlazada** (Import → Link) y agregar `sw/src` y `sw/src/lib` a los *include paths*.
4. `lscript.ld`: **stack 64 KB** (0x10000) y **heap 64 KB** (0x10000). Los buffers grandes
   (pool de notas, buffer de archivo, sprites, framebuffers del simulador) son globales `static`
   o `const`, nunca locales.
5. Flags: `-Wall -Wextra -O2` en Release; `-O0 -g` en Debug. El `BOOT.bin` se arma con Release.

### 7.2 Árbol de fuentes (`sw/src/`)

| Archivo | Bloque del Diagrama 2 | Dueño | Portable | Contenido |
|---|---|---|---|---|
| `main.c` | — | A | no | `init_all()` y bucle principal (§9.6) |
| `isr.c/.h` | ISR_handler, GIC | A | no | GIC, timers, relojes `sys_ms`/`song_ms`, `timer0/1_isr` (§8.1) |
| `input.c/.h` | Joystick_aconditioner, ISR_handler | A | no | Colas de golpes y de UI; `joy_sample`, `deb_isr`, `gpio_isr` (§8.3) |
| `joy_logic.c/.h` | Joystick_aconditioner | A | **sí** | Detector de dirección con eje dominante e histéresis (testeable en el PC) |
| `sensors.c/.h` | Microphone_ / Accelerometer_aconditioner | A | no | Shake y nivel del público (§8.4) |
| `opt3001.c/.h`, `theme.c/.h` | Light_aconditioner | A | no | Sensor de luz, umbrales y tema (§8.5) |
| `audio.c` | TTC0 (PWM) | A | no | `buzzer_tone()` sobre TTC0 (§8.6) |
| `audio_seq.c` | Sound Manager | B | **sí** | Secuenciador `buzzer_play/tick/stop` y jingles (§8.6) |
| `sd_storage.c` | SD_Manager | A | no | FatFs + respaldo embebido (§10) |
| `sd_formats.c` | SD_Manager | B | **sí** | `song_parse`, `records_parse`, `records_format` (§10.3) |
| `song_embedded.c` | — | B (generado) | **sí** | `SONG1.SHC` embebido como `const char[]` (§12.3) |
| `debouncer_axi.c/.h` | Debouncer_IP (driver) | B | no | Driver del IP (§8.2) |
| `debouncer_logic.c` | — | B | **sí** | Validación de `DebCfg` (usada por el driver y por `uart_config`) |
| `uart_config.c/.h` | UART_config | B | **sí** | Lector de línea no bloqueante y parser de la configuración (§8.7) |
| `uart_hal.c` | UART1 | A | no | `uart_hal_getc()` no bloqueante y `uart_hal_puts()` |
| `lcd_fast.c/.h` | Screen Manager | A | no | Rellenos y *blits* de sprites por ráfagas SPI (§9.5.4) |
| `arrows.c/.h` | Screen Manager | B (generado) | **sí** | Sprites 18×18 de las flechas (§9.5.3, §12.3) |
| `game_engine.c/.h` | Game Manager | B | **sí** | Reglas del juego (§9.1) |
| `game_fsm.c/.h` | State_Machine | B | **sí** | Máquina de estados y `GameCtx` (§9.2) |
| `render.c/.h` | Screen Manager | B | **sí** | Pantallas, sobre `GUI_*`, `lcd_fill` y `lcd_blit` (§9.5) |
| `game_types.h`, `interfaces.h` | — | B / ambos | **sí** | Contratos (§7.4) |
| `lib/` | drivers | curso | no | Drivers del curso, citados en la cabecera (§7.3) |

`tests/Makefile` → agregar `joy_logic.c`, `uart_config.c`, `arrows.c` y `song_embedded.c` a
`PORTABLE`.

### 7.3 Drivers del curso (`sw/src/lib/`)

Copiar sin cambios funcionales y citar en la cabecera de cada archivo:

| Archivos | Origen | Ajustes permitidos |
|---|---|---|
| `LCD_Driver.c/.h`, `LCD_GUI.c/.h`, `LCD_SPI.c/.h`, `fonts.h`, `font8/12/16/20/24.c`, `Delay.c/.h` | AYU11 2026-2 (`archivocAYUD11/`) o LAB10 (`Proyecto_Base_Booster/Archivos_Vitis`) | Ninguno, salvo exponer `SpiInstance` y `gpio0` como `extern` en un `.h` |
| `ADC.c/.h` | **LAB10** (fórmula `(rx[0] + rx[1]*256)/4`, coherente con `C_NUM_TRANSFER_BITS=16`) | Agregar `adc_read_raw(u8 ch)` genérico (§8.4) |
| `I2C.c/.h` | AYU10 (`Archivos/`) | Quitar la inicialización del TMP006 y el `init_platform()` interno. `XIic_Send/Recv` de bajo nivel se usan tal cual |
| `platform.c/.h`, `platform_config.h` | plantilla de Vitis o AYU10 (`sw/comun/`) | Ninguno |

> El driver del LCD define `LCD_WIDTH = LCD_HEIGHT = 170` y offsets `LCD_X/LCD_Y`. La pantalla
> visible de la BoosterPack es **128×128**: dibujar solo en `x, y ∈ [0, 127]` y confirmar el
> origen visible con una prueba de bordes [VERIFICAR].

### 7.4 Contratos: cambios a `game_types.h` e `interfaces.h`

Los `.h` actuales del repo son anteriores al cambio a joystick. Se **actualizan** para cumplir los
diagramas vigentes sin ambigüedad. Coordinar con el dueño (B) en un PR `docs/contratos`.

**`game_types.h`: constantes y tipos nuevos o modificados**

```c
/* --- carriles: direcciones del joystick, de izquierda a derecha en pantalla --- */
typedef enum { IZQ = 0, ARRIBA = 1, ABAJO = 2, DER = 3 } Lane;   /* ← ↑ ↓ → */
#define N_LANES           4u

typedef struct {
    u32  t_ms;   /* instante en que la nota cruza el receptor */
    Lane lane;   /* IZQ..DER                                  */
    u16  freq;   /* Hz para el buzzer (0 = silencio)          */
    u16  dur;    /* ms                                        */
} Note;

/* --- constantes nuevas o cambiadas --- */
#define ROCK_START        10        /* rock meter inicial (mitad de ROCK_MAX)          */
#define ROCK_WRONG        (-2)      /* flecha equivocada con nota en ventana (pág. 3)   */
#define LEADIN_MS         3000u     /* cuenta 3-2-1: la 1.a nota del chart cae >= 3 s   */
#define SONG_TAIL_MS      2000u     /* margen tras la última nota para declarar fin     */
#define NOTE_LOOKAHEAD_MS 1500u     /* tiempo que tarda una nota en recorrer la pista    */
#define JOY_TICK_MS       2u        /* periodo de axi_timer_1                            */
#define SENS_DIV          5u        /* acelerómetro y mic cada 5 ticks = 10 ms           */
#define FRAME_DIV         15u       /* frame cada 15 ticks = 30 ms (~33 FPS)             */
#define JOY_UMBRAL_DEF    250u      /* desviación (cuentas de 10 bits) para "dirección"  */
#define JOY_HIST          80u       /* vuelve a centro bajo JOY_UMBRAL - JOY_HIST        */
#define HIT_Q_LEN         16u       /* potencia de 2 */
#define UI_Q_LEN          8u        /* potencia de 2 */
#define ENCORE_WIN_MS     5000u     /* ventana del grito en GAME OVER                    */
#define ENCORE_LEVEL      70u       /* nivel de público (0..100) que cuenta como grito   */
#define ENCORE_FRAMES     3u        /* frames seguidos sobre ENCORE_LEVEL                */
#define ENCORE_BONUS_PCT  10u       /* +10 % del puntaje                                 */

typedef enum { STAR_CHARGING = 0, STAR_READY = 1, STAR_ACTIVE = 2 } StarPhase;

typedef struct {
    u32 score;
    u16 streak, max_streak;
    u8  star_bar;              /* 0..100 % (para el HUD)                           */
    u8  star_phase;            /* StarPhase                                         */
    u8  charge;                /* aciertos seguidos desde el último reinicio        */
    volatile u32 star_ms;      /* ms restantes de star power (lo descuenta timer0)  */
    s8  rock;                  /* 0 => GAME OVER                                    */
    u8  theme;                 /* Theme                                             */
    u8  crowd;                 /* 0..100                                            */
    u16 next_note;             /* primera nota sin juzgar                           */
    u16 hits, misses;
    u8  judged[(MAX_NOTES + 7u) / 8u];   /* bitset: nota i ya juzgada               */
    u8  rcv_fx[N_LANES];       /* frames de destello del receptor                   */
    u8  rcv_fx_kind[N_LANES];  /* 1 = acierto, 2 = fallo                            */
    u8  encore_done;
} GameState;

typedef enum { RESULT_NONE = 0, RESULT_WIN = 1, RESULT_LOSE = 2 } GameResult;

typedef struct { u8 lane; u32 song_ms; } Hit;     /* lane = Lane */

/* Eventos de interfaz. Las flechas del juego NO pasan por aquí (van a la cola de Hit). */
typedef enum { UI_NONE = 0, UI_OK, UI_BACK, UI_UP, UI_DOWN, UI_SHAKE } UiEvent;
```

**`interfaces.h`: cambios**

```c
/* DebCfg: parámetros pedidos por la UART (página 3: DEB_MS, BTN_MASK, JOY_UMBRAL, UMBRAL_SHAKE). */
typedef struct {
    u16 deb_ms;        /* tiempo de antirrebote [ms]              -> IP        */
    u8  btn_mask;      /* bit0 S1, bit1 S2, bit2 JOY_SEL          -> IP        */
    u16 joy_umbral;    /* umbral de dirección del joystick        -> input.c   */
    u16 umbral_shake;  /* umbral del acelerómetro [mg]            -> sensors.c */
} DebCfg;

/* isr.c (A): relojes. Portables a través de stubs. */
u32  clock_sys_ms(void);              /* ms desde el arranque (siempre corre)       */
u32  clock_song_ms(void);             /* ms de canción (corre solo si run = 1)      */
void clock_song_set(u32 ms);
void clock_song_run(int run);

/* input.c (A): modo del joystick. 1 = JUEGO (direcciones -> cola de Hit);
 * 0 = menús (ARRIBA/ABAJO -> UI_UP/UI_DOWN). Lo fija la FSM al entrar y salir de ST_PLAY. */
void input_set_play_mode(int play);
void input_set_joy_umbral(u16 umbral);
int  hit_pop(Hit *out);               /* 0 = vacía */
UiEvent ui_pop(void);
void input_flush(void);               /* vacía ambas colas */

/* sensors.c (A) */
void sensors_set_shake_threshold(u16 mg);

/* uart_config.c (B): versión no bloqueante (la FSM no se congela; S2 cancela). */
typedef struct { u8 step; u8 len; char line[24]; DebCfg tmp; } UartCfg;
void uart_cfg_begin(UartCfg *u, const DebCfg *current);
int  uart_cfg_poll(UartCfg *u, DebCfg *out);  /* 0 = en curso, 1 = listo, <0 = error */
/* uart_config_read(DebCfg*) queda como envoltorio bloqueante, para tests. */

/* sd_storage.c (A): respaldo transparente (ver §10). */
typedef struct { u8 mounted; } SdCtx;
int  sd_init(SdCtx *sd);                                         /* intenta montar */
int  sd_load_song(SdCtx *sd, Song *song, Note *pool, const char *path);
int  sd_load_records(SdCtx *sd, Record *r, u8 max);              /* devuelve n     */
int  sd_save_records(SdCtx *sd, const Record *r, u8 n);

/* game_engine.c (B): game_is_over devuelve GameResult. */
int  game_is_over(const GameState *gs, const Song *song, u32 song_ms);

/* audio_seq.c (B) */
void buzzer_play_jingle(Buzzer_t *bz, const Note *notes, u16 n, u8 loop);

/* lcd_fast.c (A): el simulador del PC los implementa con un framebuffer. */
void lcd_fill(u16 x0, u16 y0, u16 x1, u16 y1, u16 color);           /* x1, y1 exclusivos */
void lcd_blit(u16 x, u16 y, u16 w, u16 h, const u16 *pix);          /* RGB565, fila a fila */
```

Se eliminan `sensors_joy_event()` y `lane_mask` (reemplazados por `input.c` y `btn_mask`).

**`Buzzer_t` ampliado:**

```c
typedef struct {
    const Note *notes;
    u16 n, idx;
    u32 note_end_ms;
    u8  vol;            /* 0..100 % (duty)                                     */
    u8  playing;        /* 1 = secuenciando                                    */
    u8  loop;           /* jingles en loop (menú)                              */
    u8  song_clock;     /* 1 = usa song_ms (se pausa con la canción); 0 = reloj propio */
    u32 t_ms;           /* reloj propio del jingle (lo avanza buzzer_tick)     */
    u16 cur_f;          /* frecuencia sonando (evita reescribir el TTC)        */
} Buzzer_t;
```

---

## 8. Firmware: drivers y módulos de servicio

### 8.1 `isr.c`: GIC, timers y relojes (ISR_handler)

```c
/* isr.c - GIC, timers AXI y relojes del juego. Dueño: A. */
#include "xparameters.h"
#include "xscugic.h"
#include "xtmrctr.h"
#include "xil_exception.h"
#include "isr.h"
#include "interfaces.h"

/* IDs del GIC: usar las macros del BSP [VERIFICAR nombres]; los valores esperados son 61..64. */
#define IRQ_TMR0  XPAR_FABRIC_AXI_TIMER_0_INTERRUPT_INTR      /* 61 */
#define IRQ_TMR1  XPAR_FABRIC_AXI_TIMER_1_INTERRUPT_INTR      /* 62 */
#define IRQ_DEB   XPAR_FABRIC_DEBOUNCER_AXI_0_IRQ_INTR        /* 63 */
#define IRQ_GPIO1 XPAR_FABRIC_AXI_GPIO_1_IP2INTC_IRPT_INTR    /* 64 */

#define TMR_CLK_HZ   50000000u
#define TLR_FOR_MS(ms) ((TMR_CLK_HZ / 1000u) * (ms) - 2u)   /* modo down + auto-reload: periodo = TLR+2 */

static XScuGic gic;
static XTmrCtr tmr0, tmr1;
static volatile u32 g_sys_ms, g_song_ms;
static volatile u8  g_song_run, g_sens_div, g_frame_div;
volatile u8 g_frame_flag;        /* lo consume main (ver §9.6) */

u32  clock_sys_ms(void)        { return g_sys_ms; }
u32  clock_song_ms(void)       { return g_song_ms; }
void clock_song_set(u32 ms)    { g_song_ms = ms; }
void clock_song_run(int run)   { g_song_run = (u8)(run != 0); }

static void timer0_cb(void *ref, u8 n)          /* 1 ms: reloj de canción + buzzer */
{
    (void)n; GameCtx *ctx = (GameCtx *)ref;
    g_sys_ms++;
    if (g_song_run) {
        g_song_ms++;
        if (ctx_gs(ctx)->star_ms) ctx_gs(ctx)->star_ms--;   /* diagrama: star_ms-- en timer0 */
    }
    buzzer_tick(ctx_bz(ctx), g_song_ms);                     /* el secuenciador elige su reloj */
}

static void timer1_cb(void *ref, u8 n)          /* 2 ms: joystick, sensores, frame */
{
    (void)ref; (void)n;
    joy_sample();                                            /* cada 2 ms (input.c) */
    if (++g_sens_div  >= SENS_DIV)  { g_sens_div  = 0; sensors_sample(); }    /* 10 ms */
    if (++g_frame_div >= FRAME_DIV) { g_frame_div = 0; g_frame_flag = 1; }    /* 30 ms */
}

int isr_init(GameCtx *ctx)
{
    XScuGic_Config *cfg = XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
    if (!cfg || XScuGic_CfgInitialize(&gic, cfg, cfg->CpuBaseAddress) != XST_SUCCESS) return -1;
    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                                 (Xil_ExceptionHandler)XScuGic_InterruptHandler, &gic);

    /* Timers: modo interrupción + auto-reload + cuenta descendente. */
    XTmrCtr *t[2] = { &tmr0, &tmr1 };
    u16 id[2]  = { XPAR_AXI_TIMER_0_DEVICE_ID, XPAR_AXI_TIMER_1_DEVICE_ID };
    u32 tlr[2] = { TLR_FOR_MS(1), TLR_FOR_MS(JOY_TICK_MS) };
    XTmrCtr_Handler cb[2] = { timer0_cb, timer1_cb };
    for (int i = 0; i < 2; i++) {
        if (XTmrCtr_Initialize(t[i], id[i]) != XST_SUCCESS) return -2;
        XTmrCtr_SetHandler(t[i], cb[i], ctx);
        XTmrCtr_SetOptions(t[i], 0, XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION | XTC_DOWN_COUNT_OPTION);
        XTmrCtr_SetResetValue(t[i], 0, tlr[i]);
    }

    /* Prioridad (menor = más prioritaria) y trigger 0x1 = nivel alto. */
    XScuGic_SetPriorityTriggerType(&gic, IRQ_TMR0, 0xA0, 0x1);
    XScuGic_SetPriorityTriggerType(&gic, IRQ_TMR1, 0xA8, 0x1);
    XScuGic_SetPriorityTriggerType(&gic, IRQ_DEB,  0xB0, 0x1);
    XScuGic_SetPriorityTriggerType(&gic, IRQ_GPIO1,0xB8, 0x1);
    XScuGic_Connect(&gic, IRQ_TMR0, (Xil_InterruptHandler)XTmrCtr_InterruptHandler, &tmr0);
    XScuGic_Connect(&gic, IRQ_TMR1, (Xil_InterruptHandler)XTmrCtr_InterruptHandler, &tmr1);
    XScuGic_Connect(&gic, IRQ_DEB,  (Xil_InterruptHandler)deb_isr,  ctx);
    XScuGic_Connect(&gic, IRQ_GPIO1,(Xil_InterruptHandler)gpio_isr, ctx);
    XScuGic_Enable(&gic, IRQ_TMR0); XScuGic_Enable(&gic, IRQ_TMR1);
    XScuGic_Enable(&gic, IRQ_DEB);  XScuGic_Enable(&gic, IRQ_GPIO1);

    Xil_ExceptionEnable();
    XTmrCtr_Start(&tmr0, 0);
    XTmrCtr_Start(&tmr1, 0);
    return 0;
}
```

(`ctx_gs()` y `ctx_bz()` son accesores *inline* de `game_fsm.h`, que evitan exponer `GameCtx`
en `isr.c`; también sirven punteros globales inicializados en `isr_init`).

**Aceptación:** `clock_sys_ms()` avanza 60000 ± 5 por minuto (contrastar con `XTime_GetTime`).
Medir con `XTime` la duración de `timer1_cb` en los ticks con sensores: debe ser < 300 µs.

### 8.2 `debouncer_axi.c`: driver del IP (AO3)

```c
/* debouncer_axi.c - Driver del IP propio debouncer_axi (AO3). Dueño: B. */
#include "xil_io.h"
#include "xparameters.h"
#include "debouncer_axi.h"

#define DEB_CTRL     0x00u
#define DEB_DEB_MS   0x04u
#define DEB_BTNMASK  0x08u
#define DEB_STATE    0x0Cu
#define DEB_EDGE     0x10u
#define CTRL_EN      (1u << 0)
#define CTRL_IRQ_EN  (1u << 1)
#define CTRL_INV     (1u << 2)
#define BTN_ACTIVE_LOW 1          /* S1/S2/JOY_SEL de la Booster [VERIFICAR] */
#define DEB_WR(d, off, v) Xil_Out32((d)->base + (off), (u32)(v))
#define DEB_RD(d, off)    Xil_In32((d)->base + (off))

/* Inicializa y configura el hardware a partir de una configuración validada.
 * dev: instancia que se llena (base AXI y copia de la cfg). cfg: parámetros de la UART.
 * Retorna 0 = OK, -1 = puntero nulo, -2 = parámetros fuera de rango, -3 = read-back distinto. */
int debouncer_init(Debouncer *dev, const DebCfg *cfg)
{
    if (dev == NULL || cfg == NULL) return -1;
    if (debcfg_validate(cfg) != 0) return -2;          /* debouncer_logic.c (portable) */
    dev->base = XPAR_DEBOUNCER_AXI_0_S00_AXI_BASEADDR; /* [VERIFICAR nombre] */
    dev->cfg  = *cfg;
    DEB_WR(dev, DEB_CTRL, 0);                          /* detener mientras se configura */
    DEB_WR(dev, DEB_DEB_MS,  cfg->deb_ms);
    DEB_WR(dev, DEB_BTNMASK, cfg->btn_mask & 0x7u);
    DEB_WR(dev, DEB_EDGE,    0x7u);                    /* limpiar flancos viejos (W1C) */
    DEB_WR(dev, DEB_CTRL,    CTRL_EN | CTRL_IRQ_EN | (BTN_ACTIVE_LOW ? CTRL_INV : 0u));
    if ((DEB_RD(dev, DEB_DEB_MS)  & 0xFFu) != cfg->deb_ms ||
        (DEB_RD(dev, DEB_BTNMASK) & 0x7u)  != (cfg->btn_mask & 0x7u)) return -3;
    return 0;
}

/* Lee las pulsaciones pendientes y las borra (W1C). La relectura asegura que el W1C
 * se completó antes de salir de la ISR (evita una IRQ fantasma de nivel). */
u8 debouncer_get_edges(Debouncer *dev)
{
    u8 e = (u8)(DEB_RD(dev, DEB_EDGE) & 0x7u);
    if (e) { DEB_WR(dev, DEB_EDGE, e); (void)DEB_RD(dev, DEB_EDGE); }
    return e;
}

u8 debouncer_get_state(const Debouncer *dev) { return (u8)(DEB_RD(dev, DEB_STATE) & 0x7u); }
```

`debouncer_logic.c` (portable): `int debcfg_validate(const DebCfg *c)` comprueba
`1 ≤ deb_ms ≤ 255` (recomendado 5..50), `1 ≤ btn_mask ≤ 0x7` con el bit 0 o el bit 2 en 1 (si no,
no hay forma de dar OK en los menús), `100 ≤ joy_umbral ≤ 450` y `200 ≤ umbral_shake ≤ 2000`.
Tiene tests en el PC.

**Valores por defecto** (se aplican en INIT, así el juego funciona sin consola):
`deb_ms = 20`, `btn_mask = 0x7`, `joy_umbral = 250`, `umbral_shake = 800`.

### 8.3 `input.c`: joystick (golpes y menús) y botones (Joystick_aconditioner + ISR)

**Colas SPSC:**

```c
typedef struct { Hit buf[HIT_Q_LEN]; volatile u8 head, tail; } HitQ;   /* head: ISR, tail: main */
static HitQ hq;
static void hitq_push(const Hit *h) {
    u8 nh = (u8)((hq.head + 1u) & (HIT_Q_LEN - 1u));
    if (nh != hq.tail) { hq.buf[hq.head] = *h; hq.head = nh; }   /* llena: se descarta */
}
int hit_pop(Hit *out) {
    if (hq.tail == hq.head) return 0;
    *out = hq.buf[hq.tail]; hq.tail = (u8)((hq.tail + 1u) & (HIT_Q_LEN - 1u)); return 1;
}
/* Igual para UiEvent: ui_push() (desde las ISR) y ui_pop() (main). */
```

**Detector de dirección (`joy_logic.c`, portable)** (`SepHero_BD`, página 4):

```c
typedef enum { JS_CENTER = 0, JS_HELD = 1 } JoyPhase;
typedef struct { JoyPhase ph; Lane dir; u16 umbral; s16 x0, y0; } JoyDet;

/* Devuelve la dirección recién activada (0..3) o -1 si no hay evento nuevo.
 * Solo cuenta el paso CENTRO -> dirección; el eje con mayor desviación decide (sin diagonales).
 * Para un nuevo evento hay que volver bajo (umbral - JOY_HIST). */
int joy_detect(JoyDet *d, u16 x, u16 y)
{
    s16 dx = (s16)x - d->x0;
    s16 dy = (s16)y - d->y0;
#if JOY_INVERT_Y
    dy = (s16)-dy;
#endif
    u16 ax = (u16)(dx < 0 ? -dx : dx), ay = (u16)(dy < 0 ? -dy : dy);
    u16 mag = ax > ay ? ax : ay;
    if (d->ph == JS_CENTER) {
        if (mag > d->umbral) {
            d->dir = (ax >= ay) ? (dx < 0 ? IZQ : DER) : (dy > 0 ? ARRIBA : ABAJO);
            d->ph  = JS_HELD;
            return (int)d->dir;
        }
    } else if (mag < (u16)(d->umbral - JOY_HIST)) {
        d->ph = JS_CENTER;
    }
    return -1;
}
```

Convención: `dy > 0` = joystick hacia arriba. Si en la placa arriba da valores bajos del ADC,
definir `JOY_INVERT_Y = 1` [VERIFICAR]. Los centros `x0` e `y0` se calibran en `input_init()`
(promedio de 32 muestras con el joystick suelto).

**`joy_sample()`** (desde `timer1_isr`, cada 2 ms):

```c
void joy_sample(void)
{
    u16 x = adc_read_raw(ADC_CH_JOYX), y = adc_read_raw(ADC_CH_JOYY);
    int d = joy_detect(&g_joy, x, y);
    if (d < 0) return;
    if (g_play_mode) {                                  /* JUEGO: golpe con sello de tiempo */
        if (clock_sys_ms() < g_shake_mute_until) return; /* ver §8.4 */
        Hit h = { (u8)d, clock_song_ms() };
        hitq_push(&h);
    } else if (d == ARRIBA) ui_push(UI_UP);             /* menús: solo el eje vertical */
    else if (d == ABAJO)    ui_push(UI_DOWN);
}
```

La resolución temporal del golpe es de 2 ms (más el tiempo que tarda la palanca en cruzar el
umbral). Si se mide un retraso sistemático, compensarlo con `JOY_LAT_MS` (default 0) restándolo
del sello.

**`deb_isr(void *ref)`** (botones, ya filtrados por el IP):

```c
u8 e = debouncer_get_edges(&ctx->deb);
if (e & 0x1u) ui_push(UI_OK);      /* S1      */
if (e & 0x2u) ui_push(UI_BACK);    /* S2      */
if (e & 0x4u) ui_push(UI_OK);      /* JOY_SEL */
```

**`gpio_isr(void *ref)`** (solo OPT_INT):

```c
u32 st = XGpio_InterruptGetStatus(&gpio1);
XGpio_InterruptClear(&gpio1, st);
if ((st & XGPIO_IR_CH1_MASK) && (XGpio_DiscreteRead(&gpio1, 1) & 0x1u) == 0u)
    opt_pending = 1;               /* INT activo en bajo; el I2C lo hace theme_task() en main */
```

La AXI GPIO interrumpe en **cualquier cambio** de entrada (ambos flancos); el flanco de
desactivación se ignora porque el pin está en alto. Inicialización:
`XGpio_Initialize(&gpio1, XPAR_AXI_GPIO_1_DEVICE_ID)`, `XGpio_SetDataDirection(&gpio1, 1, 0x1)`,
`XGpio_InterruptEnable(&gpio1, XGPIO_IR_CH1_MASK)` y `XGpio_InterruptGlobalEnable(&gpio1)`.

### 8.4 `sensors.c`: ADC, acelerómetro y micrófono (AO1)

**ADC (driver del LAB10).** Canal `ch` ⇒ comando `ch << 3`, enviado en ambos bytes de una
transferencia de 16 bits. El ADC devuelve la conversión **del comando anterior**; por eso el
curso hace 2 transferencias por lectura. El valor final es de 10 bits:
`(rx[0] + rx[1]*256) / 4`.

| Canal | Comando | Señal | Lo lee |
|---|---|---|---|
| 0 | 0x00 | Joystick X | `joy_sample` (2 ms) |
| 1 | 0x08 | Joystick Y | `joy_sample` (2 ms) |
| 2 | 0x10 | Acelerómetro X | `sensors_sample` (10 ms) |
| 3 | 0x18 | Acelerómetro Y | `sensors_sample` (10 ms) |
| 4 | 0x20 | Acelerómetro Z | `sensors_sample` (10 ms) |
| 5 | 0x28 | Micrófono | `sensors_sample` (10 ms) |
| 6, 7 | 0x30, 0x38 | Potenciómetros 1 y 2 | no se usan |

Agregar en `ADC.c` la función `u16 adc_read_raw(u8 ch)`, que repite el patrón del curso con
`cmd = ch << 3`. **Optimización opcional:** leer los canales en ráfaga encadenada (enviar el
comando del canal k+1 mientras se recibe el k), con n+1 transferencias en vez de 2n.

**`sensors_init()`:** espera 200 ms con la placa quieta y promedia 32 muestras:

- Acelerómetro: los ejes se centran en 512 (salida ratiométrica, 0 g = Vcc/2). Con la placa en
  reposo, `cpg = |v_reposo|` = cuentas por g (se espera ~200 cuentas) [VERIFICAR con UART].
- Micrófono: `mic_noise` = pico a pico en silencio.

**`sensors_sample()`** (desde `timer1_isr`, cada 10 ms):

1. **Shake (Accelerometer_aconditioner → Power_up_enable):** `v = (ax−512, ay−512, az−512)`.
   Condición `|v|² · 10⁶ > ((1000 + umbral_mg) · cpg)²` (en `u64`, sin raíz). Equivale a
   `|a| − 1 g > UMBRAL_SHAKE`. Exige **2 muestras seguidas** y un período refractario de 500 ms.
   Al cumplirse ⇒ `ui_push(UI_SHAKE)`.
   *Nota de jugabilidad:* al sacudir la placa el joystick puede desviarse. Durante los 300 ms
   siguientes a un shake, `joy_sample` descarta los golpes (`g_shake_mute_until`).
2. **Micrófono → público (Microphone_aconditioner → Crowd_noise):** anillo de las últimas 8
   muestras; `p2p = max − min`; `lvl = clamp((p2p − mic_noise) · 100 / MIC_FULL, 0, 100)` con
   `MIC_FULL` calibrable (~300) y suavizado EMA `crowd = (3·crowd + lvl) / 4`. A 100 Hz el audio
   queda submuestreado; para estimar la **envolvente** (intensidad) basta.

`sensors_crowd_level()` devuelve `crowd`.

### 8.5 `opt3001.c` y `theme.c`: sensor de luz por interrupción (Light_aconditioner; AO1, AO2)

**Registros del OPT3001 (I²C 0x44)**, con la hoja de datos de la AYU10 (`opt3001.pdf`):

| Reg | Nombre | Uso en Sep Hero |
|---|---|---|
| 0x00 | `RESULT` | `lux = 0.01 · 2^E[15:12] · R[11:0]` |
| 0x01 | `CONFIG` | **Se escribe** `0xC410`: RN=1100 (rango automático), CT=0 (100 ms), M=10 (continuo), **L=1 (latch, modo ventana)**, POL=0 (INT activo en bajo), FC=00. Leerlo borra el latch y entrega FH (bit 6) y FL (bit 5) |
| 0x02 | `LOW_LIMIT` | **Se escribe** según el tema (mismo formato E/R) |
| 0x03 | `HIGH_LIMIT` | **Se escribe** según el tema |
| 0x7E / 0x7F | IDs | Deben leer `0x5449` y `0x3001` (comprobación en `opt3001_init`) |

Con esto se cumple el **"modificar ≥ 2 registros" de AO1**: CONFIG, LOW_LIMIT y HIGH_LIMIT.
La validación consiste en releer cada registro después de escribirlo, imprimir por UART los
valores escritos y leídos, y mostrar que el umbral de cambio de tema sigue a los límites.

**Codificación de lux a registro:**

```c
static u16 lux_to_reg(u32 lux) {           /* resolución 0.01 lux */
    u32 v = lux * 100u; u16 e = 0;
    while (v > 0x0FFFu && e < 11u) { v >>= 1; e++; }
    return (u16)((e << 12) | (v & 0x0FFFu));
}
```

**Histéresis con re-armado de la ventana** (página 1: "gpio_isr() → theme_isr(): lee RESULT
(lux), cambia CLARO ⇄ OSCURO y re-arma LOW / HIGH_LIMIT"). `Opt3001Cfg = { lo_lux = 40,
hi_lux = 80 }`, valores calibrables en el lugar:

| Tema actual | LOW_LIMIT | HIGH_LIMIT | INT se activa cuando |
|---|---|---|---|
| CLARO | `lux_to_reg(lo_lux)` | `0xBFFF` (máximo) | lux < lo ⇒ pasar a OSCURO |
| OSCURO | `0x0000` | `lux_to_reg(hi_lux)` | lux > hi ⇒ pasar a CLARO |

**Flujo (I²C solo fuera de ISR):**

1. `gpio_isr` ve OPT_INT en bajo ⇒ `opt_pending = 1` (no toca el I²C).
2. `theme_task()` (la parte "theme_isr" del diagrama, ejecutada en `main`): si `opt_pending`, lo
   limpia; lee `CONFIG` (borra el latch y obtiene FH/FL); lee `RESULT` y decide el tema (FL ⇒
   OSCURO, FH ⇒ CLARO; si no hay flags, compara lux con la ventana); escribe los nuevos LOW/HIGH;
   relee `CONFIG` para limpiar eventos pendientes; actualiza `g_theme` e imprime
   `"[OPT] lux=%d tema=%s LOW=0x%04x HIGH=0x%04x"`.
3. `theme_current()` (`Light_mode`) devuelve `g_theme`. El render detecta el cambio y redibuja
   todo.

El estado inicial se fija en `opt3001_init`: se lee `RESULT`, se elige el tema comparando con
`(lo + hi) / 2` y se arma la ventana.

`I2C.c` (AYU10) usa `XIic_Send(base, addr, buf, n, XIIC_STOP)` y `XIic_Recv(...)` de bajo
nivel. Para leer un registro: `Send(reg, 1 byte, XIIC_REPEATED_START)` y luego
`Recv(2 bytes, XIIC_STOP)`, con el MSB primero. Para escribir: `Send({reg, msb, lsb}, 3, XIIC_STOP)`.

### 8.6 Audio: PWM por TTC0 + secuenciador (Sound Manager, AC6)

**`audio.c` (A): `buzzer_tone(Buzzer_t *bz, u16 f_hz, u8 vol)`**

```c
#include "xttcps.h"
static XTtcPs ttc;            /* TTC0, contador 0 -> TTC0_WAVE0_OUT -> EMIO -> T14 */

int audio_hw_init(void) {
    XTtcPs_Config *c = XTtcPs_LookupConfig(XPAR_XTTCPS_0_DEVICE_ID);
    if (!c) return -1;
    int s = XTtcPs_CfgInitialize(&ttc, c, c->BaseAddress);
    if (s == XST_DEVICE_IS_STARTED) { XTtcPs_Stop(&ttc); s = XTtcPs_CfgInitialize(&ttc, c, c->BaseAddress); }
    if (s != XST_SUCCESS) return -2;
    XTtcPs_SetOptions(&ttc, XTTCPS_OPTION_INTERVAL_MODE | XTTCPS_OPTION_MATCH_MODE |
                            XTTCPS_OPTION_WAVE_POLARITY);
    return 0;
}

void buzzer_tone(Buzzer_t *bz, u16 f_hz, u8 vol) {
    XInterval interval; u8 presc;
    if (bz) bz->cur_f = f_hz;
    if (f_hz == 0u || vol == 0u) {                  /* silencio: match > intervalo => sin toggles */
        XTtcPs_SetMatchValue(&ttc, 0, 0xFFFFu);
        XTtcPs_SetInterval(&ttc, 0xFFFEu);
        return;
    }
    XTtcPs_Stop(&ttc);
    XTtcPs_CalcIntervalFromFreq(&ttc, f_hz, &interval, &presc);
    XTtcPs_SetInterval(&ttc, interval);
    XTtcPs_SetPrescaler(&ttc, presc);
    /* volumen = duty 0..50 %: el máximo de la fundamental está en 50 % */
    XTtcPs_SetMatchValue(&ttc, 0, (XMatchRegValue)((u32)interval * vol / 200u));
    XTtcPs_ResetCounterValue(&ttc);
    XTtcPs_Start(&ttc);
}
```

Este es un PWM por hardware del PS ruteado a la PL por EMIO, sin VHDL. La AYU10 genera el PWM
con un IP en VHDL; esta variante no se vio en clases (punto de originalidad). [VERIFICAR] con
el osciloscopio o el ILA que T14 entrega la frecuencia pedida.

**`audio_seq.c` (B, portable): secuenciador no bloqueante**

- `buzzer_play(bz, song)`: `notes = song->chart`, `n`, `idx = 0`, `song_clock = 1`,
  `playing = 1`. La melodía se toca **en el tiempo de la canción**: la nota i suena en
  `[t_ms, t_ms + dur)` del `song_ms`, así que la pausa la congela sola.
- `buzzer_play_jingle(bz, notes, n, loop)`: `song_clock = 0`, `t_ms = 0`. Las notas del jingle
  tienen `t_ms` relativo al inicio.
- `buzzer_tick(bz, song_ms)` (cada 1 ms, desde `timer0_isr`):
  `now = song_clock ? song_ms : bz->t_ms++`. Avanza `idx` mientras
  `now ≥ notes[idx].t_ms + notes[idx].dur`. Si `notes[idx].t_ms ≤ now`, la frecuencia deseada
  es `notes[idx].freq`; si no, 0. Llama a `buzzer_tone` **solo si cambia** respecto de
  `cur_f`. Al terminar: `loop` ⇒ `idx = 0, t_ms = 0`; si no, `playing = 0` y silencio.
- `buzzer_stop(bz)`: `playing = 0` y `buzzer_tone(bz, 0, 0)`.
- **Jingles** (`const Note[]` en `audio_seq.c`): `JINGLE_MENU` (4 compases en loop),
  `JINGLE_WIN`, `JINGLE_LOSE`, `SFX_COUNT` (beep corto de 1 kHz para el 3-2-1), `SFX_STAR`
  (arpegio ascendente) y `SFX_ENCORE`. Los SFX se tocan como jingle sin loop y luego se
  retoma la canción.

**Rango útil:** un buzzer pasivo suena mejor entre ~1 y 4 kHz. El generador de charts transpone
la melodía a la octava 5–6 [VERIFICAR de oído].

### 8.7 `uart_config.c`: configuración por consola (UART_config, AO3)

Basado en la AYU09 (consola UART con `xil_printf`, `inbyte` y lectura de enteros), pero **no
bloqueante**: la FSM sigue corriendo y S2 cancela. La HAL (`uart_hal.c`, A) entrega:

```c
int  uart_hal_getc(void) {   /* -1 si no hay byte */
    return XUartPs_IsReceiveData(STDIN_BASEADDRESS) ? (int)XUartPs_RecvByte(STDIN_BASEADDRESS) : -1;
}
void uart_hal_puts(const char *s) { xil_printf("%s", s); }
```

Protocolo (lo que ve el usuario en la terminal a 115200 8N1; los cuatro parámetros de la
página 3):

```
=== SEP HERO :: CONFIGURACION (S2 en la placa cancela) ===
DEB_MS [1-255] (actual 20): 15
BTN_MASK [hex 1-7: bit0 S1, bit1 S2, bit2 SEL] (actual 7): 7
JOY_UMBRAL [100-450] (actual 250): 220
UMBRAL_SHAKE [mg 200-2000] (actual 800): 900
OK -> DEB_MS=15 BTN_MASK=0x7 JOY_UMBRAL=220 UMBRAL_SHAKE=900 mg
IP: DEB_MS=15 BTN_MASK=0x7 (releidos del hardware)
```

- `uart_cfg_poll()` acumula caracteres con eco; maneja backspace; con `\r` o `\n` parsea el
  paso actual. Una línea vacía conserva el valor actual. Un valor inválido imprime
  `"Valor fuera de rango"` y repite el paso.
- Al terminar ⇒ devuelve 1 con `*out` validado (`debcfg_validate`). La FSM llama a
  `debouncer_init(&ctx->deb, &cfg)`, `input_set_joy_umbral(cfg.joy_umbral)` y
  `sensors_set_shake_threshold(cfg.umbral_shake)`, e imprime los registros releídos del IP como
  validación.

---

## 9. Firmware: lógica del juego, FSM y render

### 9.1 `game_engine.c` (Game Manager, portable)

```c
void game_reset(GameState *gs);   /* memset 0; rock = ROCK_START; star_phase = CHARGING; theme se conserva */
```

**`game_judge_hit(gs, song, hit)`**: busca la nota más cercana, no juzgada, del carril (flecha)
del golpe:

```
best = -1; best_dt = HIT_WIN_MS + 1; other_in_window = 0
for i in [gs->next_note, song->n):
    if chart[i].t_ms > hit->song_ms + HIT_WIN_MS: break        // el chart está ordenado por t_ms
    if judged(i): continue
    dt = |chart[i].t_ms − hit->song_ms|
    if dt > HIT_WIN_MS: continue
    if chart[i].lane == hit->lane:
        if dt < best_dt: best = i; best_dt = dt
    else: other_in_window = 1
if best >= 0:                                                  // ACIERTO
    judged(best) = 1; hits++; streak++; max_streak = max(...)
    mult = (star_phase == ACTIVE) ? 2 : 1;  score += PTS_NOTA * mult
    rock = min(ROCK_MAX, rock + ROCK_HIT)
    if star_phase == CHARGING: charge++; if charge >= STAR_STREAK: star_phase = READY
    rcv_fx[lane] = 4; rcv_fx_kind[lane] = 1
    return 1
if other_in_window:                                            // FLECHA EQUIVOCADA (pág. 3)
    streak = 0; if star_phase == CHARGING: charge = 0
    rock = max(0, rock + ROCK_WRONG); rcv_fx[hit->lane] = 4; rcv_fx_kind[hit->lane] = 2
    return 0                                                   // la nota sigue disponible
return 0                                                       // golpe sin notas cerca: se ignora
```

**`game_update(gs, song, now)`**, llamado **después** de vaciar la cola de golpes del frame
(si no, una nota podría marcarse perdida antes de juzgar un golpe ya registrado que le
correspondía):

```
for i from next_note while i < n:
    if judged(i): continue
    if chart[i].t_ms + HIT_WIN_MS < now:                       // pasó el receptor sin golpe
        judged(i) = 1; misses++; streak = 0
        if star_phase == CHARGING: charge = 0
        rock = max(0, rock + ROCK_MISS)
    else break
avanzar next_note mientras judged(next_note)
if star_phase == ACTIVE and star_ms == 0: star_phase = CHARGING; charge = 0
star_bar = CHARGING ? charge*100/STAR_STREAK : READY ? 100 : star_ms*100/STAR_MS
decrementar rcv_fx[] (un frame)
```

**`game_try_star_power(gs)`**: si `star_phase == READY` ⇒ `ACTIVE`, `star_ms = STAR_MS`,
devuelve 1; si no, 0.

**`game_is_over(gs, song, now)`**: devuelve `RESULT_LOSE` si `rock ≤ 0`; `RESULT_WIN` si
`now > t_ms(última) + dur(última) + SONG_TAIL_MS`; si no, `RESULT_NONE`.

**Sub-FSM del star power** (página 3): CARGANDO --(streak ≥ STAR_STREAK)--> LISTO (barra llena,
parpadea) --(shake)--> ACTIVO (20 s, ×2, notas azules) --(star_ms = 0)--> CARGANDO. Un fallo en
CARGANDO reinicia la carga; en LISTO la barra se conserva hasta usarla.

**Tests en el PC (obligatorios):** acierto dentro y fuera de la ventana (±120 ms en el límite);
flecha equivocada con nota en la ventana (rock −2, la nota sigue disponible y se puede acertar
después); flecha sin notas cerca (sin efecto); doble golpe sobre la misma nota (el segundo no
puntúa); nota perdida que descuenta 2 de rock; 10 aciertos ⇒ READY; shake en CHARGING no hace
nada; ×2 durante ACTIVE; fin por rock = 0; fin de canción; saturación del rock en `ROCK_MAX`.

### 9.2 `game_fsm.c`: máquina de estados (State_Machine, AC1)

**`GameCtx`** (en `game_fsm.h`):

```c
struct GameCtx {
    FsmState  st, prev_st;
    GameState gs;
    Song      song;
    Note      pool[MAX_NOTES];
    Record    rec[MAX_RECORDS]; u8 n_rec;
    DebCfg    cfg;  Debouncer deb;  UartCfg ucfg;
    Buzzer_t  bz;   SdCtx sd;
    u8  cursor;                 /* opción resaltada en menús                     */
    u8  song_sel;               /* canción elegida                               */
    u8  result;                 /* GameResult                                    */
    u32 t_state;                /* clock_sys_ms() al entrar al estado            */
    u8  name[3], name_pos, name_entry;   /* ingreso de iniciales al top 5        */
    u8  encore_cnt;
    volatile u8 frame_flag;
    u8  redraw;                 /* 1 = redibujar la pantalla completa            */
};
```

Cada transición usa `enter_state(ctx, nuevo)`: guarda `prev_st`, pone `t_state =
clock_sys_ms()` y `redraw = 1`, y ejecuta la acción de entrada. **Al entrar a `ST_PLAY`:**
`input_set_play_mode(1)` e `input_flush()`. **Al salir:** `input_set_play_mode(0)`.

Transiciones de la página 3:

| Estado | Entrada (acción) | Evento → transición |
|---|---|---|
| `ST_INIT` | (en `main`: `init_all`) logo "SEP HERO"; `sd_init`; `sd_load_records` | tras 1,5 s → `ST_MENU` |
| `ST_MENU` | `cursor = 0`; `buzzer_play_jingle(JINGLE_MENU, loop=1)` | UP/DOWN (joystick ↕): mover cursor · OK (S1): Jugar → `ST_LEVEL`, Récords → `ST_RECORDS`, Configurar → `ST_CONFIG` |
| `ST_RECORDS` | `buzzer_stop` | BACK (S2) → `ST_MENU` |
| `ST_CONFIG` | LCD "Configure por consola"; `uart_cfg_begin(&ucfg, &cfg)` | cada frame `uart_cfg_poll`: 1 → `debouncer_init` + umbrales → `ST_MENU` · BACK → `ST_MENU` (cancela) |
| `ST_LEVEL` | lista de canciones (SONG1; SONG2 si existe), con nombre, BPM y récord; preview con `buzzer_play_jingle(primeras 8 notas, loop)` | UP/DOWN: elegir · OK: `sd_load_song` → (OK) `game_reset`, `clock_song_set(0)`, `clock_song_run(1)`, `buzzer_play(&bz, &song)` → `ST_PLAY` (cuenta 3-2-1 durante el `LEADIN_MS`) · BACK → `ST_MENU` |
| `ST_PLAY` | modo juego del joystick | cada frame: vaciar `hit_pop` → `game_judge_hit`; `game_update(now)`; copiar `theme` y `crowd`; SHAKE → `game_try_star_power` (si da 1: `SFX_STAR`) · BACK → `ST_PAUSE` · `game_is_over ≠ NONE` → `ST_GAMEOVER` |
| `ST_PAUSE` | `clock_song_run(0)`; `buzzer_tone(0)`; `cursor = 0` | UP/DOWN · BACK u OK en Continuar → `clock_song_run(1)` → `ST_PLAY` · OK en Reiniciar → `game_reset`, `clock_song_set(0)`, `buzzer_play` → `ST_PLAY` · OK en Salir → `buzzer_stop` → `ST_MENU` |
| `ST_GAMEOVER` | `clock_song_run(0)`; `result = game_is_over()`; jingle WIN/LOSE; `name_entry = entra_top5(score)` | primeros 5 s: si `crowd ≥ ENCORE_LEVEL` durante `ENCORE_FRAMES` frames y `!encore_done` → `score += score*ENCORE_BONUS_PCT/100`, `SFX_ENCORE` · con `name_entry`: UP/DOWN cambia la letra (A–Z), OK avanza; tras la 3.a letra inserta y llama `sd_save_records` · OK (sin entrada pendiente) → `ST_MENU` |

**En paralelo a todos los estados** (página 3): el tema CLARO ⇄ OSCURO cambia con la
interrupción del OPT3001, sin pasar por la FSM.

**Tests en el PC:** recorrido completo MENU → LEVEL → PLAY → PAUSE → PLAY → GAMEOVER → MENU
con eventos inyectados y stubs de reloj; CONFIG con líneas simuladas (válidas, inválidas y
cancelación); que `input_set_play_mode` se llame al entrar y salir de PLAY.

### 9.3 Entradas por estado (página 3)

| Estado | Entradas activas |
|---|---|
| MENU | joystick ↕, S1 |
| RÉCORDS | S2 |
| CONFIG | consola UART, S2 |
| SELECCIÓN | joystick ↕, S1, S2 |
| JUEGO | **joystick ← ↑ ↓ → (notas)**, acelerómetro, S2, micrófono (HUD) |
| PAUSA | joystick ↕, S1, S2 |
| GAME OVER | S1, micrófono (encore) |

`fsm_step(ctx, ev)` recibe **un** evento de UI por frame (`ui_pop()` en `main`). `UI_SHAKE` solo
importa en `ST_PLAY`. Las flechas del juego no pasan por la cola de UI: tienen la suya, con sello
de tiempo.

### 9.4 Récords

Top 5 ordenado de mayor a menor. Un puntaje entra si `n_rec < 5` o si `score > rec[4].score`.
Inserción ordenada; con 6 entradas se descarta la última. Sin SD, los récords viven en RAM
durante la sesión.

### 9.5 `render.c` (Screen Manager, portable) y rendimiento del LCD

#### 9.5.1 Pantalla de juego (`SepHero_BD`, página 4)

Coordenadas en píxeles del LCD (128×128, RGB565). La página 4 es el mockup a escala ×3 y manda
sobre los valores de esta tabla; ajustar a ojo en la placa.

| # | Elemento | Posición y tamaño | Comportamiento |
|---|---|---|---|
| 1 | Puntaje | Arriba a la izquierda, `x=2, y=2`, 6 dígitos con ceros (`012450`), `Font16` | Aparece **"×2"** azul al centro arriba (`x≈60`) con star power activo |
| 2 | Público | Arriba a la derecha: 3 barras verticales crecientes en `x=108..124, y=2..13` | Barras encendidas según `crowd` (0..100 → 0..3), naranja; las apagadas en gris |
| 3 | Barra de poder | Columna izquierda `x=2..10, y=20..118` (contorno + relleno de abajo hacia arriba) | Amarilla según `star_bar`; parpadea en LISTO; **azul** en ACTIVO (se vacía con `star_ms`) |
| 4 | Rock meter | Columna derecha `x=117..125, y=20..118` | Verde; **rojo** con `rock ≤ ROCK_MAX/4` |
| 7 | Bordes de la pista | Líneas verticales de 2 px en `x=14..15` y `x=112..113`, `y=16..118` | **Grises**; **azules** con star power |
| — | Separadores de carril | Líneas de 1 px en `x=40, 64, 88` | Gris oscuro (tema oscuro) o gris claro (tema claro) |
| — | Carriles | 4 carriles de **24 px**: carril i en `x0 = 16 + 24·i` (`x = 16–112`) | ← ↑ ↓ → de izquierda a derecha |
| 6 | Receptores | Contorno de la flecha de cada carril, 18×18, en `y = 98..115` (`RCV_Y = 98`), centrado en el carril (`x0 + 3`) | Contorno gris claro (oscuro) o gris oscuro (claro). Destello de 4 frames: blanco/amarillo al acertar y rojo al fallar (`rcv_fx`) |
| 5 | Notas-flecha | Sprite 18×18 relleno, centrado en el carril | Color por carril: **magenta ←, verde ↑, amarillo ↓, rojo →**; todas **azules** con star power |
| 8 | Progreso de la canción | Barra horizontal `x=16..112, y=121..124` | Llenado `song_ms / duración`; blanco sobre gris (oscuro) o negro sobre gris (claro) |
| — | Fondo | — | **Negro** en tema OSCURO, **blanco** en tema CLARO (lo decide el OPT3001) |

**Posición vertical de una nota:**
`y_top = RCV_Y − (t_nota − now) · (RCV_Y − 18) / NOTE_LOOKAHEAD_MS`. Se dibuja si
`−HIT_WIN_MS ≤ t_nota − now ≤ NOTE_LOOKAHEAD_MS` y la nota no fue juzgada (recortada entre
`y = 16` y `y = 118`). Con 80 px en 1500 ms, cada nota baja ≈1,6 px por frame.

Colores RGB565 sugeridos: magenta `0xF81F`, verde `0x07E0`, amarillo `0xFFE0`, rojo `0xF800`,
azul de star power `0x341F`, gris borde `0x8410`, naranja `0xFD20`.

**Otras pantallas:** INIT (logo, con `IMG_LCD_Booster.py`), MENU (3 opciones con cursor), RÉCORDS
(tabla top 5), CONFIG ("Configure por consola" + valores actuales), SELECCIÓN (nombre, BPM y
récord), JUEGO (layout de arriba; los primeros 3 s, cuenta "3-2-1" en grande sobre la pista),
PAUSA (recuadro centrado con Continuar · Reiniciar · Salir sobre la pista congelada) y GAME OVER
("VICTORIA"/"DERROTA", puntaje, % de aciertos, racha máxima, récord, aviso "¡GRITA!" durante la
ventana del encore e iniciales).

#### 9.5.2 Estrategia de dibujo (obligatoria)

El driver del curso envía **cada byte en una `XSpi_Transfer`**, así que limpiar la pantalla
completa tarda más de 100 ms. Por eso:

1. **Redibujo completo** solo con `ctx->redraw = 1` (cambio de estado o de tema).
2. En `ST_PLAY`, **cada nota se mueve con un solo blit**: el rectángulo de 18 × (18 + Δy) que
   va desde el borde superior anterior hasta el borde inferior nuevo, con Δy filas de fondo
   arriba y el sprite abajo. Se arma en un buffer y se envía con `lcd_blit` (≈1 ms por nota). Se
   guardan las posiciones del frame anterior (máx. 24 notas visibles).
3. Si una nota se superpone a un receptor, se compone sobre el contorno del receptor en el
   buffer. Cuando una nota desaparece (juzgada o fuera de pantalla), se borra con el fondo y se
   repinta el receptor si correspondía.
4. HUD, barras, público y progreso: redibujar **solo si cambió el valor** (caché de los últimos
   valores mostrados).
5. Cambio a o desde star power: repintar los bordes y todas las notas visibles en el color nuevo.
6. Medir el tiempo de `render_frame` con `XTime_GetTime` (en modo debug, imprimirlo cada 100
   frames). **Presupuesto: < 25 ms por frame.**

#### 9.5.3 Sprites de las flechas (`arrows.c`, AC2)

Como en la página 1 y en la tabla de AC2: `const u16 arrow_spr[4][18*18]` (notas, rellenas) y
`const u16 arrow_rcv[4][18*18]` (receptores, solo contorno), en flash, indexados por `Lane`. Se
guardan como **máscaras en RGB565**: `0x0000` = transparente (fondo), `0xFFFF` = cuerpo y
`0x8410` = borde o sombra. Al dibujar se reemplazan por el color del carril (o azul), el color de
fondo del tema y un tono más oscuro del color. Así un mismo sprite sirve para todos los colores
y ambos temas. Los genera `tools/gen_arrows.py` (§12.3) a partir de una flecha base
(punta triangular + cuerpo rectangular) rotada 0°, 90°, 180° y 270°.

#### 9.5.4 `lcd_fast.c` (A)

- `lcd_fill(x0, y0, x1, y1, color)`: `LCD_SetWindows(x0, y0, x1, y1)` y `DC_HIGH()`, y luego
  el color en **ráfagas**: un buffer estático de 2·w bytes por fila y una sola
  `XSpi_Transfer(&SpiInstance, buf, NULL, 2*w)` por fila.
- `lcd_blit(x, y, w, h, pix)`: igual, copiando cada fila de `pix` (RGB565, MSB primero) al buffer.

En modo polled, con las interrupciones del SPI deshabilitadas, el driver rellena la FIFO solo.
Así se elimina el overhead por byte (un orden de magnitud más rápido). `render.c` usa `lcd_fill`
para rectángulos, `lcd_blit` para sprites y `GUI_DisString_EN` para texto. En el simulador del
PC, ambas escriben en el framebuffer.

### 9.6 `main.c`

```c
int main(void)
{
    static GameCtx ctx;                 /* global: ~10 KB, fuera del stack */
    init_all(&ctx);                     /* orden abajo */
    fsm_start(&ctx);                    /* ST_INIT -> logo */
    while (1) {
        theme_task();                   /* I2C del OPT3001, si hubo OPT_INT */
        if (g_frame_flag) {             /* cada 30 ms (timer1) */
            g_frame_flag = 0;
            fsm_step(&ctx, ui_pop());
            render_frame(&ctx);
        }
    }
}
```

**Orden de `init_all()`** (el GIC al final, para que ninguna ISR corra con un periférico sin
inicializar):

1. `init_platform()` (caché y UART) y banner por UART con la versión y la fecha de compilación.
2. LCD: `XGpio_Initialize(&gpio0, XPAR_AXI_GPIO_0_DEVICE_ID)`, `XSpi_Init(&SpiInstance, SPI_DEVICE_ID)`,
   `LCD_Init(SCAN_DIR_DFT)`, `LCD_Clear` y logo.
3. ADC: `init_adc(&SpiInstance1, SPI_DEVICE_ID_1)`, `input_init()` (centro del joystick: "no
   tocar el joystick") y `sensors_init()` (calibración: "no mover la placa").
4. I²C: `init_IIC()` y `opt3001_init(&optcfg)` (verifica los IDs; si falla, tema CLARO fijo y aviso).
5. `debouncer_init(&ctx.deb, &DEFAULT_CFG)`, `input_set_joy_umbral(250)` y
   `sensors_set_shake_threshold(800)`.
6. `XGpio` 1 (OPT_INT) e interrupción de canal (§8.3).
7. `audio_hw_init()`.
8. `sd_init(&ctx.sd)` (sin tarjeta: `mounted = 0`, se informa por UART) y `sd_load_records`.
9. `isr_init(&ctx)` (timers + GIC + `Xil_ExceptionEnable`).

Cada paso imprime `"[INIT] <paso> OK"` o `"[INIT] <paso> FALLO (<código>)"`.

---

## 10. Almacenamiento: microSD con respaldo embebido (SD_Manager, AC7)

### 10.1 Comportamiento

| Situación | Canciones | Récords |
|---|---|---|
| Sin microSD (hoy) | `song_parse` sobre el `const char` embebido (`song_embedded.c`), generado desde el mismo `SONG1.SHC` | En RAM (se pierden al apagar). LCD y UART avisan "SD no detectada" |
| Con microSD FAT32 (≤ 32 GB) con `SONG1.SHC` y `RECORDS.TXT` en la raíz | Se lee `0:/SONG1.SHC` (si no existe, se usa la embebida) | Se leen al inicio y se escriben (`FA_CREATE_ALWAYS`) cada vez que alguien entra al top 5 |

### 10.2 `sd_storage.c` (A)

```c
static FATFS fs;
static char  fbuf[16384] __attribute__((aligned(32)));   /* buffer de archivo */

int sd_init(SdCtx *sd) {
    sd->mounted = (f_mount(&fs, "0:/", 1) == FR_OK);       /* opt=1: monta ya (detecta si no hay tarjeta) */
    return sd->mounted ? 0 : -1;
}
int sd_read_file(const char *path, char *buf, u32 max, u32 *len) {
    FIL f; UINT br; if (f_open(&f, path, FA_READ) != FR_OK) return -1;
    FRESULT r = f_read(&f, buf, max, &br); f_close(&f); *len = br; return (r == FR_OK) ? 0 : -2;
}
int sd_write_file(const char *path, const char *buf, u32 len) {
    FIL f; UINT bw; if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return -1;
    FRESULT r = f_write(&f, buf, len, &bw); f_close(&f); return (r == FR_OK && bw == len) ? 0 : -2;
}
int sd_load_song(SdCtx *sd, Song *song, Note *pool, const char *path) {
    u32 len = 0;
    if (sd->mounted && sd_read_file(path, fbuf, sizeof fbuf, &len) == 0)
        return song_parse(fbuf, len, song, pool, MAX_NOTES);
    return song_parse(g_song1_shc, g_song1_shc_len, song, pool, MAX_NOTES);   /* respaldo */
}
/* sd_load_records / sd_save_records: idem con records_parse / records_format; sin SD: RAM. */
```

Sin tarjeta, el driver `XSdPs` detecta la ausencia por el card detect (MIO 47) y `f_mount`
devuelve error rápido. [VERIFICAR] que no se cuelga: con un timeout de 2 s, si `f_mount` no
retorna, compilar con `-DSEP_NO_SD` para saltarse el montaje.

### 10.3 Formatos (`sd_formats.c`, portable)

**`SONG1.SHC`** (texto ASCII, líneas terminadas en `\n` o `\r\n`; `#` inicia un comentario):

```
# SEP HERO chart v1
NAME=Oda a la Alegria
BPM=120
# t_ms  lane(0=IZQ 1=ARRIBA 2=ABAJO 3=DER)  freq_hz  dur_ms
3000 0 1319 450
3500 3 1319 450
4000 1 1397 450
...
```

Reglas: `NAME` (≤ 15 caracteres), `BPM` (u16) y luego una nota por línea con 4 enteros
separados por espacios. `t_ms` es no decreciente (el parser lo valida), `lane ∈ 0..3`, `freq`
0 = silencio y `dur > 0`. Máximo `MAX_NOTES`. `song_parse` devuelve el número de notas, o un
valor < 0 con el código de error (línea inválida, desorden o exceso), sin `sscanf` (parser de
enteros propio, portable y sin heap).

**`RECORDS.TXT`:**

```
# SEP HERO records v1
MPZ 18400
ABC 12100
```

Hasta 5 líneas `NNN puntaje`, con `NNN` en `[A-Z0-9]{3}`. `records_parse` ignora las líneas
inválidas y ordena de mayor a menor. `records_format` escribe la cabecera y las líneas.

**Tests en el PC:** chart válido, chart con nota desordenada, carril 4 (error), más de
`MAX_NOTES` (error), CRLF, archivo vacío; récords con líneas basura, más de 5 líneas y
reescritura (ida y vuelta).

---

## 11. Boot autónomo (AC4)

### 11.1 Hoy: boot desde la QSPI flash (sin microSD) [+]

1. Compilar `sep_hero` en **Release** y el `zynq_fsbl` de la plataforma.
2. `Xilinx → Create Boot Image` con el BIF (guardar en `boot/sep_hero.bif`):

   ```
   the_ROM_image:
   {
       [bootloader] zynq_fsbl.elf
       design_1_wrapper.bit
       sep_hero.elf
   }
   ```

   Salida: `BOOT.bin`.
3. **JP5 en JTAG**, placa encendida y conectada. `Xilinx → Program Flash`: Image File =
   `BOOT.bin`, Offset = `0x0`, Flash Type = `qspi-x4-single`, Init File (FSBL) =
   `zynq_fsbl.elf`, marcar *Verify after flash*. Equivalente por línea de comandos:

   ```bash
   program_flash -f BOOT.bin -offset 0 -flash_type qspi-x4-single -fsbl zynq_fsbl.elf -verify
   ```

4. Apagar, **JP5 en QSPI** y encender. Al terminar la carga del bitstream se enciende el LED
   DONE, el LCD muestra el logo y la UART el banner. **Aceptación:** tras un ciclo de apagado y
   encendido, el juego arranca solo, sin el PC.

### 11.2 Con microSD (como dicen los diagramas)

Copiar el **mismo `BOOT.bin`**, `SONG1.SHC` y `RECORDS.TXT` a la raíz de la tarjeta (FAT32) y
poner **JP5 en SD**. Las dos variantes cumplen AC4; la de SD además ejercita AC7 desde el
arranque. Guardar en `sd_card/` los archivos que van a la tarjeta.

---

## 12. Herramientas en el PC: tests, simulador y generadores

### 12.1 Tests (`tests/`)

`make -C tests run` compila los módulos portables con `stubs.c` y los `test_*.c`. Los stubs
deben cubrir `clock_*`, `buzzer_tone`, `sd_*`, `debouncer_init`, `input_*`, `sensors_*`,
`theme_current`, `uart_hal_*`, `lcd_fill`, `lcd_blit` y los `GUI_*`. Hay un archivo de tests por
módulo: `test_engine.c`, `test_fsm.c`, `test_joy.c`, `test_formats.c`, `test_audio_seq.c`,
`test_debcfg.c` y `test_uart_cfg.c`, con los casos de §9.1, §9.2 y §10.3. Si falla un caso, el
ejecutable retorna ≠ 0.

**`test_joy.c`** (detector de dirección): centro sin evento; cruce del umbral en cada una de las
4 direcciones; diagonal con `|dx| > |dy|` ⇒ dirección horizontal; mantener la palanca no repite
el evento; volver por debajo de `umbral − JOY_HIST` rearma el detector; oscilar alrededor del
umbral (dentro de la histéresis) no genera eventos extra; `JOY_INVERT_Y`.

### 12.2 Simulador del render (`sim/`)

`sim/gui_stub.c` implementa `GUI_DrawRectangle`, `GUI_DrawLine`, `GUI_DisString_EN`,
`GUI_DisChar`, `LCD_Clear`, `LCD_SetArealColor`, `lcd_fill` y `lcd_blit` sobre un framebuffer
`u16 fb[128][128]`, enlazando las fuentes `font*.c` del curso (son tablas C portables).
`sim/main_sim.c` arma un `GameCtx` en cada estado (incluidas las 3 variantes de la página 4:
tema oscuro, star power y tema claro con rock bajo), llama a `render_frame` y escribe
`sim/out/<estado>.ppm` (P6, RGB888 convertido desde RGB565). Sirve para comparar con el mockup
de la página 4 y para las capturas del informe, sin la placa.

### 12.3 Generadores (`tools/`)

- **`make_chart.py`**: entrada = melodía como lista `(nota, duración_en_tiempos)`; parámetros
  `--bpm 120 --transpose +24 --leadin 3000 --name "Oda a la Alegria"`. Hace:
  `t = leadin + Σ beats·60000/BPM`; `dur = 0,9·beats·60000/BPM` (articulación);
  `freq = 440·2^((midi−69)/12)` redondeada.
  **Asignación de flechas (estilo DDR, siguiendo el contorno melódico):** la altura sube ⇒
  `ARRIBA`; baja ⇒ `ABAJO`; se repite ⇒ alternar `IZQ`/`DER`. Regla de jugabilidad: como hay que
  volver al centro entre dos golpes del mismo carril, si dos notas seguidas del mismo carril
  quedan a menos de `MIN_SAME_LANE_MS = 250` ms, la segunda pasa al carril horizontal opuesto.
  Escribe `sd_card/SONG1.SHC`. Melodía sugerida: *Oda a la Alegría* (Beethoven, dominio
  público), tema completo de 16 compases.
- **`shc2c.py`**: convierte `SONG1.SHC` en `sw/src/song_embedded.c` con
  `const char g_song1_shc[] = "...";` y `const u32 g_song1_shc_len`. Correrlo cada vez que
  cambie el chart (el `.c` generado se versiona).
- **`gen_arrows.py`**: genera `sw/src/arrows.c/.h` (`arrow_spr` y `arrow_rcv`, 4 × 18×18) con el
  formato de máscara de §9.5.3. Exporta también un `.ppm` de vista previa.
- **`IMG_LCD_Booster.py`** (repositorio del curso PROY02): convierte el logo a arreglo RGB565 para
  `GUI_Disbitmap`. Citarlo.

---

## 13. Plan de implementación por etapas y aceptación

Cada etapa se cierra con su aceptación demostrada en la placa (o en el PC si se indica) y con un
PR.

| Etapa | Contenido | Aceptación |
|---|---|---|
| **E0** Contratos | Aplicar §7.4 a los `.h`; stubs; copiar los diagramas vigentes al repo; `make -C tests run` compila | Compila en el PC sin warnings |
| **E1** BD base | PS7 (§4.2), interconnect, LCD (SPI0+GPIO0), ADC (SPI1), IIC, `.xdc`, `.xsa` | Programa de prueba: LCD con texto, valores del ADC (joystick y acelerómetro) por UART, IDs del OPT3001 = 0x5449/0x3001 |
| **E2** Timers + GIC | `axi_timer_0` (1 ms) y `axi_timer_1` (2 ms), `xlconcat`, `isr.c` | `sys_ms` exacto (§8.1); `frame_flag` a 33 Hz; contador en el LCD |
| **E3** Joystick | `joy_logic.c` + tests, `joy_sample` | Cada dirección imprime `[JOY] dir=← t=...` por UART una sola vez por movimiento; diagonales resueltas por el eje dominante; eje Y verificado |
| **E4** IP propio | VHDL + TB (T1..T10 en verde), empaquetado, en el BD, driver, `deb_isr` | En la placa: S1/S2/SEL ⇒ `[BTN] ...` por UART sin dobles disparos; `BTN_MASK` y `DEB_MS` surten efecto |
| **E5** Boot QSPI (AC4) | §11.1 con el programa de E4 | Arranca solo tras apagar y encender |
| **E6** VIO/ILA (AC5) | §6 | Capturas de los 3 experimentos de §6.2 |
| **E7** Audio (AC6) | TTC0 + EMIO, `audio_seq.c`, jingles, `make_chart.py`, `shc2c.py` | La canción suena desde el chart embebido, sin bloquear el bucle |
| **E8** Sensores (AO1) | `sensors.c` | UI_SHAKE confiable por UART; nivel de público reacciona a la voz |
| **E9** Luz + tema (AO1, AO2) | OPT_INT (§15.1), `opt3001.c`, `theme.c` | Tapar el sensor ⇒ tema oscuro; destapar ⇒ claro; log de registros escritos/leídos |
| **E10** Juego | `game_engine`, `game_fsm`, `render` (sprites de flecha), `lcd_fast`, `gen_arrows.py`, tests y simulador | Partida completa jugable con el joystick; pantalla coherente con la página 4; render < 25 ms por frame; tests en verde |
| **E11** UART config (AO3) | `uart_config.c` + HAL | Cambiar DEB_MS/BTN_MASK/JOY_UMBRAL/UMBRAL_SHAKE desde la consola y verificarlo (VIO/ILA para el IP) |
| **E12** SD (AC7) | `sd_storage.c`, formatos | Sin tarjeta: respaldo correcto. Con tarjeta: carga el chart de la SD y persiste los récords tras reiniciar |
| **E13** Cierre | Comentarios, `write_bd_tcl`, `.xsa`, `BOOT.bin` final, export de los proyectos | El zip exportado abre y compila en otro PC |

Orden crítico: E1 → E2 → E3 (el control del juego) → E4 (AO3). E5 se puede hacer apenas exista E1.

---

## 14. Verificación de las 10 actividades

| Act. | Qué exige el enunciado | Dónde se cumple | Cómo se demuestra |
|---|---|---|---|
| **AO1** | Pantalla dinámica + ≥ 3 periféricos de la Booster (luz, temperatura, acelerómetro, micrófono) + modificar ≥ 2 registros | Screen Manager (`render.c`: pista ← ↑ ↓ → con receptores y notas-flecha, puntaje, barra de poder, rock meter); acelerómetro (star power); luz (tema); micrófono (público y encore); OPT3001 `CONFIG`, `LOW_LIMIT`, `HIGH_LIMIT` | Partida en vivo; sacudir ⇒ star power; tapar el sensor ⇒ cambia el tema; gritar ⇒ sube el indicador de público; log UART de los registros escritos y releídos |
| **AO2** | ≥ 2 interrupciones de timers de Vivado + 1 interrupción del sensor de luz o temperatura | `axi_timer_0` (1 ms: reloj y buzzer), `axi_timer_1` (2 ms: joystick; acelerómetro y mic cada 10 ms; frames), INT del OPT3001 → `axi_gpio_1` → GIC 64 | Mostrar `isr_init` y el BD; el tema cambia por interrupción (log `[OPT]`) |
| **AO3** | IP propio que interactúa con un periférico y con el procesador; init con punteros; parámetros desde la consola UART | `debouncer_axi_0` (S1, S2, JOY_SEL; AXI4-Lite; irq); `debouncer_init(Debouncer *dev, const DebCfg *cfg)`; `uart_config.c` (DEB_MS, BTN_MASK) | Configurar DEB_MS/BTN_MASK por consola y comprobarlo en los menús y en el ILA |
| **AC1** | Máquina de estados | `game_fsm.c` (8 estados + sub-FSM del star power) | Recorrer todos los estados en el video; tests de la FSM |
| **AC2** | Estructuras, arreglos, punteros, funciones y macros | `Lane` (enum), `Note`, `Song`, `GameState`, `GameCtx`, `Buzzer_t`, `Debouncer`, `DebCfg`, `Record[5]`, `chart[]`, colas circulares, sprites `arrow_spr[4][18*18]`, bitset `judged[]`, macros `HIT_WIN_MS`, `JOY_UMBRAL`, `STAR_*`, `DEB_*` | Revisión del código (rúbrica: uso correcto de punteros) |
| **AC3** | 2 periféricos adicionales distintos de los de AO1 | **Joystick** (las 4 flechas del juego y los menús) y **botones de la Booster** (S1/S2/JOY_SEL, filtrados por el IP propio) | Jugar y navegar los menús |
| **AC4** | Boot sin subir el código cada vez | `BOOT.bin` en la QSPI (hoy) o en la SD (diagramas) | Apagar, encender y ver el juego arrancar solo |
| **AC5** | VIO e ILA para validar transacciones; diferencias | `vio_0`, `ila_0` sobre `debouncer_axi_0` | Los 3 experimentos de §6.2 y la tabla de §6.3 |
| **AC6** | Buzzer con música coherente | Sound Manager → TTC0 → EMIO → Buzzer; melodía del chart + jingles | La canción suena sincronizada con las notas |
| **AC7** | Memoria SD externa y hacer algo con ella | SD_Manager: `SONG1.SHC` + `RECORDS.TXT` con xilffs | Requiere tarjeta: récord que persiste tras reiniciar y chart editado en la SD que cambia el juego. **Pendiente de conseguir la microSD** |

---

## 15. Puntos abiertos, riesgos y solución de problemas

### 15.1 Pin del INT del OPT3001 [VERIFICAR, bloquea AO2]

- La **AYU10** usa el OPT3001 solo por I²C (sondeo de `RESULT`). Ni su `.xdc` ni su código
  cablean el pin INT, así que no sirve para confirmarlo.
- El diagrama lo deja "por confirmar en el esquemático del adaptador (JD3 libre)". "JD3" es el
  **par 3 del Pmod JD** en la nomenclatura del `.xdc` maestro: `jd_p[3]` = **U14** (pin JD7) y
  `jd_n[3]` = **U15** (pin JD8). Son los dos únicos pines libres de JD.
- En la BOOSTXL-EDUMKII, el INT del OPT3001 sale por un pin del header de la BoosterPack (según
  la guía de usuario de TI, J1.8 [VERIFICAR]). Lo que importa es **a qué pin Pmod lo lleva la
  tarjeta de extensión**. Buscarlo en el esquemático de la tarjeta (Lectura 10 y repositorio
  `REF-Documentacion` del curso; ver el README del LAB10).
- **Candidatos:** U14 y U15 primero; luego los demás pines libres de los Pmod usados: JC1 V15,
  JC2 W15, JC3 T11, JC4 T10, JE8 U17, JE9 T17, JE10 Y17.
- **Procedimiento "buscador de pin"** si no hay esquemático: build temporal con `axi_gpio_1` de
  **9 bits** sobre los 9 candidatos, todos con `PULLUP true`. El firmware alterna el bit **POL**
  (bit 3) del `CONFIG` del OPT3001 cada 500 ms (`0xC410` ↔ `0xC418`), lo que invierte el nivel del
  pin INT al instante, e imprime los 9 bits por UART. El bit que alterna en fase con POL es el INT.
  También sirve el ILA sobre esas 9 entradas.
- Si la tarjeta **no** lleva el INT a ningún Pmod: puente con un cable desde el pin INT del
  header de la Booster a U14 (JD7), documentado en el informe. AO2 exige la interrupción del
  sensor; el sondeo no la reemplaza.

### 15.2 Otros puntos a verificar en la placa

| # | Punto | Cómo resolverlo |
|---|---|---|
| 1 | MOSI del LCD: `io0_o` vs `io1_o` | §4.4 |
| 2 | Polaridad de S1/S2/JOY_SEL (se asume activo en bajo ⇒ `CTRL.INV = 1`) | Leer `STATE` del IP con `INV = 0` en reposo y presionando |
| 3 | Signo del eje Y del joystick (página 4: "verificar en la placa el signo del eje Y") | Imprimir `y` crudo con la palanca arriba; ajustar `JOY_INVERT_Y` |
| 4 | `JOY_UMBRAL` y `JOY_HIST` cómodos | Probar 200–300 con la consola; ver si hay golpes fantasma al soltar la palanca |
| 5 | Escala y canales del ADC | Imprimir los crudos: joystick al centro ≈ 512; en reposo, Z del acelerómetro distinto de X/Y |
| 6 | Área visible y offsets del LCD | Dibujar un marco de 1 px en 0 y 127 |
| 7 | Cuentas por g del acelerómetro | Calibración de `sensors_init` + log |
| 8 | Rango audible del buzzer | Barrido de 500 Hz a 5 kHz desde la consola; elegir el `--transpose` |
| 9 | Nombres `XPAR_*` (IP propio, IRQ, TTC) | Revisar `xparameters.h` del BSP tras generar la plataforma |
| 10 | `f_mount` sin tarjeta no se cuelga | §10.2 |
| 11 | `SCK_RATIO` del ADC a 50 MHz | Si las lecturas son ruidosas o saturan, subir a 4 u 8 |

### 15.3 Problemas típicos

| Síntoma | Causa probable | Solución |
|---|---|---|
| No entra ninguna interrupción | Falta `Xil_ExceptionEnable`, ancho de `IRQ_F2P` distinto o trigger mal configurado | Revisar §8.1 y que el BD tenga `xlconcat` → `IRQ_F2P` |
| La IRQ del debouncer queda pegada | No se hizo el W1C o falta la relectura | `debouncer_get_edges` (§8.2) |
| El BSP no trae `XPAR_FABRIC_DEBOUNCER_*` | `irq` no declarado como interfaz de interrupción | §5.5, paso 3 |
| Vivado no toma los cambios del IP | No se subió la core revision | Re-empaquetar subiéndola, refrescar el catálogo y hacer upgrade |
| `STATE` del IP en 1 sin presionar | Polaridad invertida | `CTRL.INV` (§5.3) |
| Golpes repetidos con una sola flecha | Histéresis insuficiente o ruido del ADC | Subir `JOY_HIST`; promediar 2 lecturas por eje |
| Golpes perdidos en flechas rápidas | No se volvió al centro entre notas del mismo carril | Regla `MIN_SAME_LANE_MS` en `make_chart.py` (§12.3) |
| Pantalla lenta o parpadeo | Redibujo completo por frame o driver byte a byte | §9.5.2 y §9.5.4 |
| `song_ms` se atrasa | ISR demasiado larga (I²C o dibujo en una ISR) | Mover a `main` (§3.3) |
| Golpes juzgados como fallo | `game_update` antes de vaciar la cola | §9.1 |
| Program Flash falla | JP5 no está en JTAG, FSBL equivocado o QSPI deshabilitada en el PS | §11.1 y §4.2 |
| El proyecto no abre en otro PC | Ruta con espacios o tildes, o falta el `ip_repo` | Clonar en una ruta simple; `create_project.tcl` agrega el `ip_repo` |

---

## 16. Referencias y material base del curso

Rutas locales del material ya descargado (bajo `D:\Sep_martin_pena\Ayudantias\`). Citar cada
archivo reutilizado en la cabecera y en el informe.

| Tema | Material | Se usa para |
|---|---|---|
| Diagramas vigentes | OneDrive `Proyecto 2 SEP/Diagramas de bloques/`: `Diagrama_de_bloques_02.pdf` y `SepHero_BD.pdf` (4 páginas) | Fuente de verdad del diseño (§0) |
| Enunciado y rúbricas | Repositorio `IEE2463-SEP/PROY02` (rama 2026-2): `Proyecto02_IEE2463.pdf`, `Rubrica_Avance_Proyecto_02_IEE2463.pdf` | Definición de AO1–AO3 y AC1–AC7 |
| ILA y VIO | `AY04/AYUD04-ILA-VIO.pdf`, `AY04/LEEME-AY04.md` | §6 (trigger condicionado, VIO de estímulo) |
| IP con AXI propio | `AY05_RGB/AYUD05-AXI-RGB.pdf`, `AY05_RGB/AYU05/ip_repo/RGB_driver_1.0` | §5 (empaquetado, AXI4-Lite, ILA en modo AXI) |
| UART | `AYU09_UART/AYUD09-UART-2026-2/` (`AYUD09-UART.pdf`, `helloworld.c`) | §8.7 (consola, `inbyte`, structs/punteros) |
| I²C, OPT3001, buzzer, botones | `AYU10_I2C_Buzzer_Lux/` (`Archivos/I2C.c`, `main.c`, `opt3001.pdf`, `Zybo-Z7-Master.xdc`) | §8.5, pines I²C, S1/S2/SEL y buzzer |
| SPI: LCD y ADC | `AYU11_SPI/AYUD11-SPI-2026-2/archivocAYUD11/` | §7.3 (LCD_Driver, LCD_GUI, fuentes) |
| Periféricos Zybo + Booster | `LAB10_Peripherals_Zybo_Booster/.../Proyecto_Base_Booster.zip` (BD `DEMO.bd`, `Archivos_Vitis/`), `Zybo-Z7-Master.xdc` | §4.3 (configuración de los SPI), §8.4 (ADC y canales), pines LCD/ADC |
| Debouncer base | `D:\Sep_martin_pena\P1_FInal\debouncer\...\debouncer.vhd` (Proyecto 1 del grupo) | §5.4.1 |
| Hojas de datos | OPT3001 (`AYU10/opt3001.pdf`); Zybo Z7 Reference Manual (Digilent); BOOSTXL-EDUMKII User's Guide (TI); PG153 AXI Quad SPI, PG079 AXI Timer, PG144 AXI GPIO, PG090 AXI IIC (Xilinx); UG585 Zynq TRM (TTC, GIC) | Registros, temporización |
| Imágenes al LCD | `IMG_LCD_Booster.py` (repositorio PROY02) | Logo |
