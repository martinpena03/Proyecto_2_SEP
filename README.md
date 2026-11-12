# Sep Hero

Proyecto 2 de IEE2463 Sistemas Electrónicos Programables (PUC). Es un juego tipo "guitar hero" en la
**Zybo Z7-10 + BoosterPack**, con el ARM Cortex-A9 del PS (Vitis bare-metal) y un IP propio en
la PL (Vivado 2020.1).

- **Reparto de trabajo, hitos y flujo de GitHub:** [`docs/PLAN_TRABAJO.md`](docs/PLAN_TRABAJO.md)
- **Versión de avance (esta rama):** [`docs/AVANCE.md`](docs/AVANCE.md)
- **Diagrama de bloques:** [`docs/diagramas/Diagrama_de_bloques_02_01.pdf`](docs/diagramas/Diagrama_de_bloques_02_01.pdf); los nombres de bloques, señales y archivos lo siguen ([`docs/NOMBRES_DIAGRAMA.md`](docs/NOMBRES_DIAGRAMA.md))
- **Enunciado y rúbricas:** repositorio del curso [IEE2463-SEP/PROY02](https://github.com/IEE2463-SEP/PROY02)

## Estructura

```
hw/
  bd/            sephero_bd.tcl (write_bd_tcl del block design)       [A]
  constraints/   *.xdc                                                [A]
  ip_repo/       debouncer_axi/ (IP propio empaquetado: src/ + component.xml) [B]
  export/        sephero.xsa (para que B compile en Vitis sin Vivado) [A]
  scripts/       create_project.tcl (reconstruye el proyecto)
sw/src/          código C de la aplicación Vitis
  game_types.h   tipos compartidos
  interfaces.h   contrato entre A y B (firmas de todos los módulos)
  lib/           drivers del curso (LCD, ADC, I2C), citados en el informe
tests/           tests en el PC de la lógica portable (gcc) + stubs del hardware
sim/             simulador de screen_manager.c en el PC (genera .ppm)            [B]
tools/           scripts (generador de SONG1.SHC, conversor de imágenes)  [B]
boot/            BOOT.BIN, .bif y pasos del boot desde la SD             [A]
sd_card/         archivos que van a la raíz de la microSD (SONG1.SHC, RECORDS.TXT)
docs/            plan, diagramas, capturas VIO/ILA, slides del video
```

## Primeros pasos

```bash
git clone https://github.com/martinpena03/Proyecto_2_SEP.git
```

**Vivado** (no clonar en rutas con espacios ni tildes: Vivado falla, por ejemplo, dentro de OneDrive):

```bash
vivado -mode batch -source hw/scripts/create_project.tcl
```

**Vitis:** crear el workspace en `sw/workspace/` (está ignorado), una plataforma desde
`hw/export/sephero.xsa` (con la librería `xilffs`) y una aplicación vacía. Luego agregar
`sw/src/` como **carpeta enlazada** (Import → Link), así los cambios quedan en el repo.

**Tests en el PC** (no necesitan la placa):

```bash
make -C tests run
```

En Windows sirve `gmake` o `mingw32-make` (vienen con Strawberry Perl o MSYS2) o WSL.

## Reglas

1. No hacer push a `main`. Trabajar con ramas `feat/<tarea>` y abrir un pull request.
2. Cada archivo tiene un dueño (ver el plan). Los `.h` de `sw/src/` son el contrato entre los dos:
   los cambios se conversan antes.
3. El IP se edita en `hw/ip_repo/debouncer_axi/src/` y se re-empaqueta **subiendo la core revision**.
   Si no, Vivado no lo refresca en el block design.
4. Todo código externo (curso, ayudantías, internet o IA) se referencia en la cabecera del archivo
   y en el informe.
