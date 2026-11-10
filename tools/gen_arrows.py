#!/usr/bin/env python3
"""
gen_arrows.py - Genera los sprites de las flechas de Sep Hero (sw/src/arrows.c/.h).

Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
(SepHero_BD.pdf página 4, docs/MANUAL_IMPLEMENTACION.md §9.5.3 y §12.3).

Dibuja una flecha base apuntando hacia arriba (punta triangular + cuerpo rectangular)
en una grilla de 18x18 y la rota para las otras 3 direcciones. Produce dos juegos:

  arrow_spr[4][18*18]  notas (rellenas):  0x0000 = transparente, 0xFFFF = cuerpo,
                                           0x8410 = borde
  arrow_rcv[4][18*18]  receptores (solo el contorno): 0x0000 o 0xFFFF

Son MASCARAS en RGB565: render.c reemplaza cada valor por el color del carril (o azul
en star power), un tono más oscuro para el borde y el color de fondo del tema. Así un
mismo sprite sirve para todos los colores y para los dos temas.

Índice del primer subíndice = Lane: 0 IZQ, 1 ARRIBA, 2 ABAJO, 3 DER.

Uso:  python tools/gen_arrows.py          (escribe sw/src/arrows.c, sw/src/arrows.h
                                           y tools/out/arrows_preview.ppm)
"""
import os

N = 18                      # lado del sprite en píxeles
BODY, EDGE, NONE = 0xFFFF, 0x8410, 0x0000
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def up_arrow():
    """Máscara booleana NxN de una flecha que apunta hacia arriba."""
    m = [[False] * N for _ in range(N)]
    cx = (N - 1) / 2.0                       # eje de simetría (8.5)
    for y in range(N):
        for x in range(N):
            if 1 <= y <= 9:                  # punta: triángulo de vértice (cx, 1) y base en y = 9
                half = (y - 0.5) * 8.5 / 8.5
                inside = abs(x - cx) <= half
            elif 10 <= y <= 16:              # cuerpo: rectángulo de 8 px de ancho
                inside = 5 <= x <= 12
            else:
                inside = False
            m[y][x] = inside
    return m


def border_of(m, thick=1):
    """Píxeles del cuerpo que están a 'thick' o menos del exterior (4-vecindad iterada)."""
    edge = [[False] * N for _ in range(N)]
    cur = [row[:] for row in m]
    for _ in range(thick):
        nxt = [row[:] for row in cur]
        for y in range(N):
            for x in range(N):
                if not cur[y][x]:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if xx < 0 or yy < 0 or xx >= N or yy >= N or not cur[yy][xx]:
                        edge[y][x] = True
                        nxt[y][x] = False
                        break
        cur = nxt
    return edge


def flip_v(m):          # ARRIBA -> ABAJO
    return m[::-1]


def transpose(m):       # ARRIBA -> IZQ (la punta queda a la izquierda)
    return [[m[x][y] for x in range(N)] for y in range(N)]


def flip_h(m):          # IZQ -> DER
    return [row[::-1] for row in m]


def build():
    up = up_arrow()
    shapes = [transpose(up), up, flip_v(up), flip_h(transpose(up))]   # IZQ, ARRIBA, ABAJO, DER
    spr, rcv = [], []
    for m in shapes:
        e1 = border_of(m, 1)
        e2 = border_of(m, 2)
        spr.append([[(EDGE if e1[y][x] else BODY) if m[y][x] else NONE for x in range(N)]
                    for y in range(N)])
        rcv.append([[BODY if (m[y][x] and e2[y][x]) else NONE for x in range(N)]
                    for y in range(N)])
    return spr, rcv


def c_array(name, data):
    out = ["const u16 %s[N_LANES][ARROW_W * ARROW_H] = {" % name]
    lanes = ["IZQ", "ARRIBA", "ABAJO", "DER"]
    for li, img in enumerate(data):
        out.append("    {   /* %s */" % lanes[li])
        for row in img:
            out.append("        " + ", ".join("0x%04X" % v for v in row) + ",")
        out.append("    },")
    out.append("};")
    return "\n".join(out)


HEADER = """/*
 * arrows.%s - Sprites 18x18 de las flechas (notas y receptores) de Sep Hero.
 *
 * ARCHIVO GENERADO por tools/gen_arrows.py: no editar a mano.
 * Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio.
 *
 * Son máscaras RGB565: 0x0000 = transparente (fondo), 0xFFFF = cuerpo, 0x8410 = borde.
 * render.c las colorea al dibujar. Primer índice = Lane (IZQ, ARRIBA, ABAJO, DER).
 * Quedan en flash (const). Cubre parte de AC2 (arreglos bidimensionales).
 */
"""


def write_c(spr, rcv):
    h = HEADER % "h" + """#ifndef ARROWS_H
#define ARROWS_H

#include "game_types.h"

#define ARROW_W 18u
#define ARROW_H 18u
#define ARROW_BODY 0xFFFFu
#define ARROW_EDGE 0x8410u

extern const u16 arrow_spr[N_LANES][ARROW_W * ARROW_H];   /* notas (rellenas)      */
extern const u16 arrow_rcv[N_LANES][ARROW_W * ARROW_H];   /* receptores (contorno) */

#endif /* ARROWS_H */
"""
    c = HEADER % "c" + '#include "arrows.h"\n\n' + c_array("arrow_spr", spr) + "\n\n" + \
        c_array("arrow_rcv", rcv) + "\n"
    with open(os.path.join(ROOT, "sw", "src", "arrows.h"), "w", newline="\n") as f:
        f.write(h)
    with open(os.path.join(ROOT, "sw", "src", "arrows.c"), "w", newline="\n") as f:
        f.write(c)


def write_preview(spr, rcv):
    """PPM de vista previa: fila de notas (coloreadas) y fila de receptores, x8."""
    colors = [(248, 0, 248), (0, 252, 0), (248, 252, 0), (248, 0, 0)]
    S = 8
    W, H = 4 * (N + 2) * S, 2 * (N + 2) * S
    img = [[(0, 0, 0)] * W for _ in range(H)]
    for li in range(4):
        for r, data in enumerate((spr, rcv)):
            for y in range(N):
                for x in range(N):
                    v = data[li][y][x]
                    if v == NONE:
                        continue
                    col = colors[li] if v == BODY else tuple(c // 2 for c in colors[li])
                    if r == 1:
                        col = (200, 200, 200)
                    for sy in range(S):
                        for sx in range(S):
                            img[(r * (N + 2) + 1 + y) * S + sy][(li * (N + 2) + 1 + x) * S + sx] = col
    out = os.path.join(ROOT, "tools", "out")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "arrows_preview.ppm"), "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (W, H))
        for row in img:
            f.write(bytes(v for px in row for v in px))


if __name__ == "__main__":
    s, r = build()
    write_c(s, r)
    write_preview(s, r)
    print("arrows.c / arrows.h generados (%d sprites de %dx%d)" % (8, N, N))
