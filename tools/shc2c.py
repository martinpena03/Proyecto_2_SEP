#!/usr/bin/env python3
"""
shc2c.py - Convierte los charts sd_card/*.SHC (DEMO y SONGn) en sw/src/sd_manager_songs.c.

Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
(docs/MANUAL_IMPLEMENTACION.md §10.1 y §12.3).

Los charts quedan embebidos en el .elf como cadenas constantes. Son el RESPALDO que
usa sd_manager.c cuando no hay microSD (o cuando el archivo no está en la tarjeta): se
interpretan con el mismo song_parse() que los archivos de la SD.

Correr cada vez que cambie un chart; el .c generado se versiona.

Uso:  python tools/shc2c.py
"""
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
# (archivo, nombre del arreglo en C), en el orden de la selección de canción.
SONGS = [("DEMO.SHC", "g_demo_shc"), ("SONG1.SHC", "g_song1_shc"), ("SONG2.SHC", "g_song2_shc")]


def c_string(text):
    lines = []
    for ln in text.splitlines():
        esc = ln.replace("\\", "\\\\").replace('"', '\\"')
        lines.append('    "%s\\n"' % esc)
    return "\n".join(lines)


if __name__ == "__main__":
    out = ["""/*
 * sd_manager_songs.c - Charts embebidos (respaldo cuando no hay microSD).
 *
 * ARCHIVO GENERADO por tools/shc2c.py desde sd_card/DEMO.SHC y SONGn.SHC: no editar a mano.
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 */
#include "interfaces.h"
"""]
    for name, cname in SONGS:
        with open(os.path.join(ROOT, "sd_card", name), "r") as f:
            text = f.read()
        out.append("/* %s */" % name)
        out.append("const char %s[] =\n%s;" % (cname, c_string(text)))
        out.append("const u32 %s_len = (u32)(sizeof(%s) - 1u);\n" % (cname, cname))
    with open(os.path.join(ROOT, "sw", "src", "sd_manager_songs.c"), "w", newline="\n") as f:
        f.write("\n".join(out))
    print("sd_manager_songs.c generado (%d charts)" % len(SONGS))
