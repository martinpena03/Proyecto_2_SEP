#!/usr/bin/env python3
"""
ppm2png.py - Convierte imágenes PPM (P6) a PNG sin dependencias externas.

Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio.
Sirve para ver las capturas del simulador (sim/out/*.ppm) y los sprites generados
(tools/out/*.ppm), y para incluirlas en el informe.

Uso:  python tools/ppm2png.py archivo.ppm [archivo2.ppm ...] [--scale N]
      Escribe archivo.png junto a cada .ppm (N = factor de ampliación entero).
"""
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # Cabecera: "P6" ancho alto maxval, separados por espacios en blanco.
    tokens, pos = [], 0
    while len(tokens) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":                 # comentario
            while data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos])
    pos += 1                                          # un único separador tras maxval
    if tokens[0] != b"P6":
        raise ValueError("%s no es un PPM P6" % path)
    w, h = int(tokens[1]), int(tokens[2])
    return w, h, data[pos:pos + 3 * w * h]


def write_png(path, w, h, rgb, scale=1):
    if scale > 1:                                     # ampliación por vecino más cercano
        rows = []
        for y in range(h):
            row = rgb[3 * w * y:3 * w * (y + 1)]
            big = b"".join(row[3 * x:3 * x + 3] * scale for x in range(w))
            rows.extend([big] * scale)
        rgb, w, h = b"".join(rows), w * scale, h * scale
    raw = b"".join(b"\x00" + rgb[3 * w * y:3 * w * (y + 1)] for y in range(h))

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


if __name__ == "__main__":
    args = sys.argv[1:]
    scale = 1
    if "--scale" in args:
        i = args.index("--scale")
        scale = int(args[i + 1])
        del args[i:i + 2]
    for p in args:
        w, h, rgb = read_ppm(p)
        out = p[:-4] + ".png" if p.lower().endswith(".ppm") else p + ".png"
        write_png(out, w, h, rgb, scale)
        print(out)
