# Sep Hero · Reparto de trabajo

Proyecto 2 IEE2463 (SEP): Guitar Hero en la Zybo Z7-10 + BoosterPack, solo con el ARM Cortex-A9 del PS.
El diagrama de bloques completo está en [`docs/diagramas/SepHero_BD.pdf`](diagramas/SepHero_BD.pdf).

## Criterio del reparto

La restricción principal es que **la Zybo la tiene Martín en su casa**. Por eso:

- **Martín (A)** se queda con todo lo que solo se puede probar sobre la placa: el block design de Vivado,
  los periféricos del BoosterPack, las interrupciones, el buzzer, la SD, el boot y VIO/ILA.
- **Compañero (B)** se queda con todo lo que se puede **desarrollar y verificar sin la placa**:
  el IP Core en VHDL (se valida con testbench en simulación), y la lógica del juego en C
  (se compila y se prueba en el PC con `gcc`, contra *stubs* del hardware).
- Las interfaces entre ambos se fijan **desde el día 1** en los `.h` de [`sw/src/`](../sw/src/).
  Así los dos trabajan en paralelo y la integración consiste en enlazar, no en reescribir.
- Informe y video se reparten por mitades. Cada uno escribe y explica lo que implementó
  (la rúbrica pide decir qué archivos escribió el grupo y comentarlos en detalle).

Las horas son estimaciones para comprobar que la carga queda pareja. No son un compromiso.

## Tabla de responsabilidades

### Martín (A): plataforma, periféricos e integración en la placa (≈ 47 h)

| # | Tarea | Actividad | Archivos | h |
|---|---|---|---|---|
| A1 | Block design completo (PS, interconnect, SPI×2, IIC, GPIO×2, timers×2, concat, EMIO del TTC0), `.xdc`, exportar `.xsa` y `write_bd_tcl` al repo | base | `hw/bd/`, `hw/constraints/`, `hw/export/` | 8 |
| A2 | LCD (driver del curso) y lectura del ADC por SPI: joystick, acelerómetro y micrófono. `sensors.c`: shake, nivel del público y dirección del joystick con histéresis | AO1, AC3 | `sensors.c/.h`, `lib/` | 8 |
| A3 | OPT3001 por I²C: escribir `CONFIG`, `LOW_LIMIT` y `HIGH_LIMIT`, leer `RESULT`. Interrupción OPT_INT por `axi_gpio_1` CH2 → `theme.c` (claro/oscuro). **Confirmar el pin OPT_INT** | AO1, AO2 | `opt3001.c/.h`, `theme.c/.h` | 6 |
| A4 | `axi_timer_0` (1 ms) y `axi_timer_1` (10 ms) con XScuGic. `isr.c` y `input.c`, con S1, S2 y JOY_SEL por `axi_gpio_1` CH1 | AO2 | `isr.c/.h`, `input.c/.h` | 4 |
| A5 | Buzzer a bajo nivel: TTC0 en modo interval + match → PWM por EMIO. `buzzer_tone(&bz, f, vol)` | AC6 | `audio.c` (parte HW) | 3 |
| A6 | SD con xilffs: `f_mount`, lectura y escritura. Boot: FSBL + `BOOT.BIN` en la SD | AC7, AC4 | `sd_storage.c` (parte HW), `boot/` | 6 |
| A7 | VIO + ILA sobre el IP de B (sondear `btn_raw`, `EDGE` e `irq`), con capturas para el informe | AC5 | BD, `docs/` | 4 |
| A8 | Integración en la placa de cada PR de B, depuración y grabación del funcionamiento en la Zybo para el video | todas | — | 8 |

### Compañero (B): IP Core propio, lógica del juego y contenido (≈ 47 h)

| # | Tarea | Actividad | Archivos | h |
|---|---|---|---|---|
| B1 | IP `debouncer_axi` en VHDL: antirrebote configurable (`DEB_MS`), detector de flanco, registros AXI4-Lite (`CTRL`, `DEB_MS`, `LANE_MASK`, `EDGE`, `STATE`) e `irq`. **Testbench y simulación** en xsim. Empaquetado como IP (subir la core revision en cada cambio) | AO3 | `hw/ip_repo/debouncer_axi/` | 10 |
| B2 | Driver C `debouncer_init(Debouncer *dev, const DebCfg *cfg)` y `debouncer_get_edges()`. `uart_config.c`: pedir `DEB_MS`, `LANE_MASK` y `UMBRAL_SHAKE` por consola y validarlos | AO3 | `debouncer_axi.c/.h`, `uart_config.c/.h` | 5 |
| B3 | `game_engine.c` (juzgar hits, puntaje, streak, star power ×2, rock meter, encore) y `game_fsm.c` (INIT → MENU → NIVEL → JUEGO ⇄ PAUSA → GAME OVER, RÉCORDS, CONFIG). **Tests en el PC** | AC1, AC2 | `game_engine.c/.h`, `game_fsm.c/.h`, `tests/` | 10 |
| B4 | `render.c`: menús, pista de 4 carriles, notas, HUD, pantalla final y paleta según el tema y el star power. **Simulador en el PC**: stub de `GUI_*` que escribe un `.ppm` de 128×128 para ver las pantallas sin la placa | AO1 | `render.c/.h`, `sim/` | 9 |
| B5 | Formato de `SONG1.SHC` y script en Python que genera el chart desde una melodía. Elegir la melodía. Secuenciador `buzzer_tick()` (avanza notas). Formato de `RECORDS.TXT` y su parser/serializador (portable, probado en el PC) | AC6, AC7 | `tools/`, `audio.c` (secuenciador), `sd_storage.c` (parser) | 7 |
| B6 | Slides del diagrama (1 a 3) para el video, y edición final del video (OBS + VSDC) | video | `docs/video/` | 6 |

### Compartido (≈ 8 h cada uno)

| Tarea | A | B |
|---|---|---|
| Informe (LaTeX, Overleaf) | Hardware/Vivado, periféricos y registros, timers e interrupciones, boot, VIO vs ILA | IP Core, arquitectura del software, FSM, estructuras y punteros, formato de la SD |
| Introducción, conclusiones y referencias | conclusiones | introducción y referencias |
| Video (5 a 15 min) | parte 4 (demo en la Zybo) y parte 3 de Vivado | partes 1 y 2 (diagrama y checklist) y parte 3 de Vitis |
| Code review | revisa los PR de B | revisa los PR de A |

### Cobertura de las 10 actividades

| Actividad | Responsable | Dónde |
|---|---|---|
| AO1: pantalla + 3 periféricos + 2 registros | A (periféricos, OPT3001), B (contenido de la pantalla) | `sensors.c`, `opt3001.c`, `render.c` |
| AO2: 2 timers + interrupción del sensor de luz | A | `isr.c`, `theme.c` |
| AO3: IP propio + función con punteros + config por UART | B | `hw/ip_repo/debouncer_axi`, `debouncer_axi.c`, `uart_config.c` |
| AC1: máquina de estados | B | `game_fsm.c` |
| AC2: structs, arreglos, punteros, funciones, macros | B (y ambos) | `game_types.h`, `game_engine.c` |
| AC3: 2 periféricos extra (joystick, botones S1/S2) | A | `sensors.c`, `input.c` |
| AC4: boot | A | `boot/` |
| AC5: VIO e ILA | A | BD + capturas |
| AC6: buzzer con música | A (PWM), B (melodía y secuenciador) | `audio.c` |
| AC7: SD | A (FatFs), B (formatos y parser) | `sd_storage.c`, `tools/` |

## Cómo trabajar sin que B tenga la placa

1. **B nunca queda bloqueado por el hardware.** Todo lo de B se valida antes en el PC:
   el VHDL con testbench y el C con `make -C tests` (gcc, MinGW o WSL). Los `.c` del juego
   **no incluyen `xparameters.h` ni drivers de Xilinx**: hablan con el hardware solo a través de
   las funciones de los `.h` de A (`audio.h`, `sensors.h`, etc.). En el PC esas funciones se
   reemplazan por stubs.
2. **B puede compilar el proyecto Vitis completo** con el `.xsa` que A sube a `hw/export/`.
   Solo le falta correrlo.
3. **Sesión de integración semanal** (presencial en casa de Martín, en el laboratorio o en las
   jornadas PROG). A carga los PR de B en la Zybo y graba un video corto si algo falla.
4. **Las evaluaciones PROG son sorpresa y la rúbrica pone nota 1.0 si "mi proyecto lo tiene mi
   compañero que no vino".** Martín lleva la Zybo **a todas** las jornadas PROG, y los dos tienen
   que poder explicar todo el proyecto. Esa es otra razón para el code review cruzado.

## Hitos

La rúbrica del avance fija lo que se espera en cada jornada PROG. Ajustar las fechas a la
programación semestral.

| Hito | Qué tiene que estar funcionando | A | B |
|---|---|---|---|
| H0 · ya | Repo creado, B con acceso, `.h` de interfaz acordados | invitar a B | leer el diagrama y los `.h` |
| H1 · PROG06 (**AC4 boot**) | BD base + LCD + timers. Boot desde la SD con un programa simple | A1, A4, A6 (boot) | B1 (IP simulado), B3 (FSM en el PC) |
| H2 · PROG07 (**AC5 VIO/ILA**) | IP de B integrado en el BD, VIO/ILA sobre él | A7, A2 | B2, B4 |
| H3 · PROG08 (**AC6 buzzer**) | Canción sonando desde `SONG1.SHC` | A5, A6 (SD) | B5 |
| H4 · Avance formal | Diagrama + 2 actividades obligatorias completas (se sugiere AO1 y AO3) | A3 | B3 + B4 en la placa |
| H5 · Entrega final | Todo integrado, informe, video, zip exportado | A8 | B6 |

## Flujo en GitHub

- `main` siempre compila. Nadie hace push directo a `main`.
- Una rama por tarea: `feat/A3-opt3001`, `feat/B1-debouncer-ip`, `fix/...`, `docs/...`.
- Pull request hacia `main` con una descripción corta de qué cambió y cómo se probó.
  El otro lo revisa (lee el código y comenta) antes del merge.
- Los PR que tocan hardware o el `.xsa` los mergea A después de probarlos en la placa.
- Cada archivo tiene un dueño (las tablas de arriba). Si necesitas cambiar un `.h` del otro,
  avísale o abre un issue antes: los `.h` son el contrato entre los dos.
- Issues para bugs y pendientes, con las etiquetas `A`, `B`, `hw`, `sw`, `informe` y `video`.
- Antes de la entrega final, **B descarga el zip exportado y verifica que abre y compila en su PC**
  (lo pide el enunciado).
