#!/usr/bin/env python3
"""
shc2c.py - Convierte los charts sd_card/SONGn.SHC en sw/src/sd_manager_songs.c.

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
SONGS = ["SONG1.SHC", "SONG2.SHC"]


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
 * ARCHIVO GENERADO por tools/shc2c.py desde sd_card/SONGn.SHC: no editar a mano.
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero.
 */
#include "interfaces.h"
"""]
    for i, name in enumerate(SONGS, start=1):
        with open(os.path.join(ROOT, "sd_card", name), "r") as f:
            text = f.read()
        out.append("/* %s */" % name)
        out.append("const char g_song%d_shc[] =\n%s;" % (i, c_string(text)))
        out.append("const u32 g_song%d_shc_len = (u32)(sizeof(g_song%d_shc) - 1u);\n" % (i, i))
    with open(os.path.join(ROOT, "sw", "src", "sd_manager_songs.c"), "w", newline="\n") as f:
        f.write("\n".join(out))
    print("sd_manager_songs.c generado (%d charts)" % len(SONGS))
