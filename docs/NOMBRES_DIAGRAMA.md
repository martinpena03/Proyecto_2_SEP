# Nombres del diagrama de bloques en el código (versión de avance)

Referencia: [`docs/diagramas/Diagrama_de_bloques_02_01.pdf`](diagramas/Diagrama_de_bloques_02_01.pdf).
Cada bloque del diagrama tiene el mismo nombre en el block design de Vivado o en `sw/src/`,
en minúsculas cuando es un archivo C. El avance usa solo una parte de los bloques; los que
faltan están en la rama `proyecto-final` con los mismos nombres.

## FPGA (block design `design_1`, `hw/scripts/02_build_bd.tcl`)

| Bloque del diagrama | Celda del block design | Señales del diagrama (redes y puertos) |
|---|---|---|
| Debouncer_IP | `Debouncer_IP` (IP propio `debouncer_axi`) | puerto `btn_raw[2:0]`, red `irq_deb` |
| axi_quad_spi_1 | `axi_quad_spi_1` | `spi_rtl` (ADC SPI, solo acelerómetro) |
| axi_iic_0 | `axi_iic_0` | `iic_rtl` (I²C del OPT3001) |
| axi_timer_0 · 1 ms | `axi_timer_0` | red `irq_t0` |
| axi_timer_1 · 2 ms | `axi_timer_1` | red `irq_t1` |
| GIC | GIC del PS (`processing_system7_0`) | red `IRQ_F2P` (de `xlconcat_0`) |
| UART1, TTC0 | periféricos del PS (`processing_system7_0`) | |
| EMIO | EMIO del PS | red `TTC0_WAVE0_OUT` → puerto `BUZZER_PWM` |

No están en el avance: `axi_gpio_1` (OPT_INT), `axi_quad_spi_0` + `axi_gpio_0` (LCD) y SDIO0.

## ARM (`sw/src/`)

| Bloque del diagrama | Archivos |
|---|---|
| ISR_handler | `isr_handler.c` (GIC, timers, ISR de `irq_deb`) |
| Accelerometer_aconditioner | `accelerometer_aconditioner.c` |
| Light_aconditioner | `light_aconditioner.c` (OPT3001 por I²C, revisado cada 200 ms) |
| State_Machine | `state_machine.c`, `state_machine.h` |
| Game Manager | `game_manager.c` |
| SD_Manager | `sd_manager.c` (canciones embebidas y récords en RAM), `sd_manager_formats.c`, `sd_manager_songs.c` |
| Sound Manager | `sound_manager.c` |
| TTC0 (PWM) | `ttc0_pwm.c` |
| UART1 | `uart1.c` |
| UART_config | `uart_config.c`; `uart_console.c` (consola por teclado que maneja la FSM) |
| Driver del Debouncer_IP | `debouncer_ip.c`, `debouncer_ip_logic.c` (`debouncer_init(Debouncer*, const DebCfg*)`) |

No están en el avance: Joystick_aconditioner, Microphone_aconditioner y Screen Manager.

Archivos de apoyo que no son bloques: `main.c`, `event_queues.c` (colas de `Btn_edges`
entre las ISR y main), `hw.h`, `interfaces.h`, `game_types.h` y los drivers del curso en `lib/`.
