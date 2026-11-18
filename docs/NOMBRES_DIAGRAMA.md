# Nombres del diagrama de bloques en el código

Referencia: [`docs/diagramas/Diagrama_de_bloques_02.drawio`](diagramas/Diagrama_de_bloques_02.drawio)
(la misma versión que `Diagrama_de_bloques_02_02.drawio` en OneDrive; el PDF
[`Diagrama_de_bloques_02_01.pdf`](diagramas/Diagrama_de_bloques_02_01.pdf) es la exportación
anterior). Cada bloque y cada señal del diagrama tiene el mismo nombre en el block design de
Vivado o en `sw/src/`. En C se sigue esta convención:

- **archivos, variables, campos y funciones** en minúsculas: `Game_over_signal` →
  `game_over_signal`, `Light_mode` → `light_mode`;
- **tipos** con la misma grafía del diagrama: `Hit_signal`, `Game_status`, `Record`, `Note`;
- **constantes de enum** en mayúsculas: `Power_up_enable` → `UI_POWER_UP_ENABLE`.

Esta tabla es la del **proyecto completo** (rama `proyecto-final`). La versión de avance
usa los mismos nombres, con menos bloques (ver `docs/NOMBRES_DIAGRAMA.md` en la rama `avance-p2`).

## FPGA (block design `design_1`, `hw/scripts/02_build_bd.tcl`)

| Bloque del diagrama | Celda del block design | Señales del diagrama (redes y puertos) |
|---|---|---|
| Debouncer_IP | `Debouncer_IP` (IP propio `debouncer_axi`) | puertos `btn_raw[2:0]` y `leds[3:0]` (LD0–LD3), red `irq_deb` |
| axi_quad_spi_1 | `axi_quad_spi_1` | `spi_rtl` (ADC SPI) |
| axi_iic_0 | `axi_iic_0` | `iic_rtl` (I²C del OPT3001 y del TMP006) |
| axi_gpio_1 | `axi_gpio_1` | puerto `SENS_INT[7:0]` (DRDY del TMP006; INT del OPT3001 si se encuentra), red `irq_gpio` |
| axi_timer_0 · 1 ms | `axi_timer_0` | red `irq_t0` |
| axi_timer_1 · 2 ms | `axi_timer_1` | red `irq_t1` |
| axi_quad_spi_0 + axi_gpio_0 | `axi_quad_spi_0`, `axi_gpio_0` | `SCLK`, `MOSI`, `CS`, `RS`, `RST` (LCD) |
| GIC | GIC del PS (`processing_system7_0`) | red `IRQ_F2P` (de `xlconcat_0`) |
| UART1, SDIO0, TTC0 | periféricos del PS (`processing_system7_0`) | |
| EMIO | EMIO del PS | red `TTC0_WAVE0_OUT` → puerto `BUZZER_PWM` |

## ARM (`sw/src/`)

| Bloque del diagrama | Archivos |
|---|---|
| ISR_handler | `isr_handler.c` (GIC, timers, ISR de `irq_deb` e `irq_gpio` (DRDY y OPT_INT)) |
| Joystick_aconditioner | `joystick_aconditioner.c`, `joystick_aconditioner_logic.c` |
| Microphone_aconditioner | `microphone_aconditioner.c` |
| Accelerometer_aconditioner | `accelerometer_aconditioner.c` |
| Light_aconditioner | `light_aconditioner.c` (OPT3001 y tema claro/oscuro) |
| Temperature_aconditioner | `temperature_aconditioner.c` (TMP006 por I²C, interrupción del DRDY, aviso a `TEMP_ALERT_C`) |
| State_Machine | `state_machine.c`, `state_machine.h` |
| Game Manager | `game_manager.c` |
| SD_Manager | `sd_manager.c`, `sd_manager_formats.c`, `sd_manager_songs.c` |
| Sound Manager | `sound_manager.c` |
| TTC0 (PWM) | `ttc0_pwm.c` |
| Screen Manager | `screen_manager.c`, `screen_manager_arrows.c/.h`, `screen_manager_text.c/.h`, `screen_manager_lcd.c` |
| UART1 | `uart1.c` |
| UART_config | `uart_config.c`; `uart_console.c` (consola de diagnóstico por la misma UART) |
| Driver del Debouncer_IP | `debouncer_ip.c`, `debouncer_ip_logic.c` (`debouncer_init(Debouncer*, const DebCfg*)`) |

Archivos de apoyo que no son bloques: `main.c`, `event_queues.c` (colas de `Btn_edges`,
`Joy_dir` y `Hit_signal` entre las ISR y main), `hw.h`, `interfaces.h`, `game_types.h` y los
drivers del curso en `lib/`.

## Señales entre bloques

| Señal del diagrama | Origen → destino | En el código |
|---|---|---|
| `btn_raw[2:0]` | Btn → Debouncer_IP | puerto `btn_raw[2:0]` (`sephero.xdc`, `02_build_bd.tcl`) |
| `Vx, Vy`, `audio`, `aX, aY, aZ` | sensores → ADC SPI | canales `ADC_CH_JOYX/JOYY`, `ADC_CH_MIC`, `ADC_CH_ACX/ACY/ACZ` (`lib/ADC.h`) |
| `SPI (10 bits)` | ADC SPI → axi_quad_spi_1 | interfaz `spi_rtl` |
| `Data/Clk (i2c)` | Light_Sensor, Temperature_Sensor → axi_iic_0 | interfaz `iic_rtl` |
| `DRDY` | Temperature_Sensor → axi_gpio_1 | un bit de `SENS_INT[7:0]` (lo detecta `tmp006_detect_drdy()`, queda en `g_tmp_bit`) |
| `irq_deb`, `irq_gpio`, `irq_t0`, `irq_t1` | periféricos → GIC | redes del BD; `IRQ_DEB`, `IRQ_GPIO1`, `IRQ_TMR0/1` en `isr_handler.c` |
| `IRQ_F2P` | GIC → ISR_handler | red `IRQ_F2P` (`xlconcat_0`) |
| `Btn_edges[2:0]` | Debouncer_IP → State_Machine | `btn_edges` en `deb_isr()` → `ui_push(UI_OK / UI_BACK)`; JOY_SEL → `g_mute_req` |
| `joystick x, y` | axi_quad_spi_1 → Joystick_aconditioner | `joy_sample()` lee `ADC_CH_JOYX/JOYY` (`g_last_x`, `g_last_y`) |
| `microphone` | axi_quad_spi_1 → Microphone_aconditioner | `sensors_feed_mic(microphone)` (misma ráfaga del joystick, `ADC_CH_MIC`) |
| `aX, aY, aZ` | axi_quad_spi_1 → Accelerometer_aconditioner | `sensors_acc_sample()` (`g_acc[3]`) |
| `light_level` | axi_iic_0 → Light_aconditioner | `light_level` (lux) en `theme_task()` y en la inicialización del OPT3001 |
| `Hit_signal (lane, t_ms)` | Joystick_aconditioner → Game Manager | tipo `Hit_signal {lane, t_ms}`; `hit_push()` → `hit_pop()` → `game_judge_hit()` |
| `Joy_dir (menú)` | Joystick_aconditioner → State_Machine | `joy_dir` en `joy_sample()` → `ui_push(UI_UP / UI_DOWN)` fuera del juego |
| `Crowd_noise` | Microphone_aconditioner → Game Manager | `g_crowd_noise`, `sensors_crowd_noise()` → `game_status.crowd_noise` (encore) |
| `Power_up_enable` | Accelerometer_aconditioner → Game Manager | evento `UI_POWER_UP_ENABLE` → `game_try_star_power()` |
| `Light_mode` | Light_aconditioner → Screen Manager | `light_mode_current()` → `game_status.light_mode` (paleta del LCD y LEDs del modo claro) |
| `song_ms, frame_flag` | ISR_handler → Game Manager | `clock_song_ms()`, `frame_flag` |
| `TMP_DRDY` | ISR_handler → Temperature_aconditioner | `tmp_drdy` (lo levanta `gpio_isr()`, lo atiende `temp_task()`) |
| `Game_on_off` | State_Machine → Game Manager | `game_on_off()` en `state_machine.c` (corre o congela `song_ms`) |
| `Game_over_signal` | Game Manager → State_Machine | `game_over_signal` (= `game_is_over()`, un `GameResult`) |
| `Reset` | State_Machine → Game Manager | `game_reset()` |
| `State (n°)` | State_Machine → Screen Manager | `ctx->state` (`FsmState`) |
| `Game_status` | Game Manager → Screen Manager | tipo `Game_status`, campo `ctx->game_status` |
| `Notas a desplegar` | Game Manager → Screen Manager | `ctx->song.chart` + `clock_song_ms()` leídos por `render_frame()` |
| `Song chart (Note[])`, `Record[5]` | Game Manager ↔ SD_Manager | `Note`, `sd_load_song()`; `Record rec[MAX_RECORDS]`, `sd_load_records()` / `sd_save_records()` |
| `Song_enabler / Select_song` | Game Manager → Sound Manager | `song_enabler` (`ttc0_pwm.c`, lo alterna JOY_SEL), `ctx->select_song` + `buzzer_play()` |
| `Song (freq, dur)` | SD_Manager → Sound Manager | `Song`, `Note.freq`, `Note.dur` |
| `freq, duty` | Sound Manager → TTC0 (PWM) | `buzzer_tone(bz, freq, duty)` |
| (EMIO) | TTC0 → Buzzer | red `TTC0_WAVE0_OUT` → puerto `BUZZER_PWM` |
