# Sep Hero: versión de avance (rama `avance-p2`)

Ecosistema reducido del proyecto: solo las conexiones del avance. Todo se muestra por la
UART (115200 8N1, COM de la Zybo) y con los LEDs LD0–LD3; **la pantalla no se usa**.
El proyecto completo (LCD, joystick, micrófono, microSD, VIO/ILA, silencio con JOY_SEL)
está en la rama `proyecto-final`.

## Bloques y conexiones

Los nombres siguen [`Diagrama_de_bloques_02_01`](diagramas/Diagrama_de_bloques_02_01.pdf);
la correspondencia bloque → archivo está en [`NOMBRES_DIAGRAMA.md`](NOMBRES_DIAGRAMA.md).

| Bloque | Conexión | Uso en el avance |
|---|---|---|
| PS7 (ARM) | UART1, QSPI (boot), TTC0 → EMIO → `BUZZER_PWM` (T14, JD1) | consola, melodía del menú |
| `axi_timer_0` | IRQ → GIC 61, 1 ms | relojes y secuenciador del buzzer |
| `axi_timer_1` | IRQ → GIC 62, 2 ms | acelerómetro cada 10 ms, OPT3001 cada 200 ms, paso de la FSM cada 30 ms |
| `axi_quad_spi_1` | Pmod JC (ADC de la tarjeta de extensión) | acelerómetro X, Y, Z |
| `axi_iic_0` | Pmod JD9/JD10 (SCL/SDA) | OPT3001 (0x44) |
| `Debouncer_IP` (IP propio) | `btn_raw[2:0]` = S1, S2, JOY_SEL (JD2–JD4); `leds` = LD0–LD3; `irq_deb` → GIC 63 | botones filtrados y LEDs del modo claro |

Se quitaron `axi_quad_spi_0` y `axi_gpio_0` (LCD), `axi_gpio_1` (el INT del OPT3001 no
llega a la Zybo), VIO, ILA y la SD0 del PS.

## Actividades

- **AO2: interrupciones.**
  - *Luz:* el comparador de ventana del OPT3001 (registros LOW/HIGH_LIMIT, modo latch)
    detecta por hardware cuando el lux cruza el umbral. La interrupción de `axi_timer_1`
    pide revisarlo cada 200 ms, y main lee las banderas FH/FL por I2C. El resultado se
    imprime como `*** [AO2] Mucha luz ***` (> 80 lux, modo claro, LEDs ON) o
    `*** [AO2] Poca luz ***` (< 40 lux, LEDs OFF).
  - *Acelerómetro:* la ISR de `axi_timer_1` lee el ADC cada 10 ms. Si |a| − 1 g supera
    el umbral en 2 muestras seguidas, cuenta un evento y main imprime
    `*** [AO2] Movimiento detectado ***`.
- **AO3: IP propio `debouncer_axi`.**
  - *Antirrebote:* sincroniza S1, S2 y JOY_SEL y solo acepta un cambio estable durante
    `DEB_MS` ms. Guarda cada pulsación en `EDGE` (W1C) e interrumpe al GIC. Cada
    interrupción se avisa por consola: `*** [IRQ] Debouncer_IP (GIC 63): S1 -> OK ***`.
  - *LEDs:* el registro `LED_CTRL` (0x14) maneja LD0–LD3. Con el bit `THEME_LIGHT`
    activo enciende el patrón elegido por el usuario; es el habilitador del modo claro,
    que más adelante cambiará la paleta del LCD.
  - *Configuración por punteros:* `debouncer_init(Debouncer *dev, const DebCfg *cfg)`
    recibe `DEB_MS`, `BTN_MASK` y `LED_PATTERN` desde la consola (estado CONFIG), los
    escribe, los relee del hardware y devuelve un código de error.
- **AC1: máquina de estados.** INIT → MENU → (LEVEL → PLAY ↔ PAUSE → GAMEOVER |
  RECORDS | CONFIG). Al entrar a cada estado se imprime `ESTADO: X` con sus teclas.
  Los niveles existen solo en la máquina de estados, sin música: LEVEL informa el nivel
  elegido y PLAY imprime `Juego iniciado` y una cuenta regresiva de 20 s
  (`[PLAY] quedan N s`, se congela en PAUSE). Al cumplirse pasa a GAMEOVER: victoria si
  el rock meter sigue sobre 0 (con autoplay `p`), derrota si no.
- **AC6: buzzer.** Arpegio de 4 compases a 120 BPM en loop mientras se está en el MENU.
  JOY_SEL lo silencia o lo reanuda (`[BUZ] JOY_SEL -> musica OFF/ON`).

## Controles

| Placa | Teclado (consola) | Acción |
|---|---|---|
| S1 | `1` | OK |
| S2 | `2` | atrás / pausa |
| JOY_SEL | — | música ON / OFF (silencio enclavado) |
| — | `u` / `j` | subir / bajar el cursor |
| sacudir la placa | `k` | star power (en PLAY) |
| — | `p` | autoplay de la canción (no hay joystick) |
| — | `s` `o` `d` `t` `T` `h` | estado, sensor de luz, IP, interrupciones, buzzer, ayuda |

## Construcción

El hardware ya viene exportado en `hw/export/sephero.xsa` (con bitstream); Vivado solo hace
falta si se cambia el block design.

```bash
vivado -mode batch -source hw/scripts/02_build_bd.tcl   # opcional: regenera el .xsa
xsct sw/scripts/vitis_platform.tcl    # plataforma + app sep_hero con sw/src enlazada
sh sw/scripts/build_app.sh debug      # buzzer en mudo
sh sw/scripts/build_app.sh sound      # con sonido
xsct sw/scripts/run_on_board.tcl Sound
make -C tests run                     # lógica portable en el PC
```

**Desde la GUI de Vitis 2020.1:** después de `vitis_platform.tcl`, abrir Vitis con el
workspace `sw/workspace` de esta rama (en `File > Switch Workspace > Other...`). La app
`sep_hero` muestra el código en `src/sw_src` (es `sw/src` enlazada). Compilar con el martillo
y cargar con `Run As > Launch on Hardware (System Debugger)`, con el jumper JP5 en JTAG y un
terminal serial en el COM de la Zybo a 115200 8N1. Ojo: la build de la GUI tiene el buzzer
habilitado.
