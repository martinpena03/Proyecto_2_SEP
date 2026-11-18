#!/usr/bin/env python3
"""
make_chart.py - Genera los charts de Sep Hero (sd_card/SONG1.SHC y SONG2.SHC) a partir
de melodías escritas como listas (nota, duración en tiempos).

Proyecto 2 IEE2463 (SEP). Autores: grupo Sep Hero. Fuente: diseño propio
(docs/MANUAL_IMPLEMENTACION.md §12.3). Melodías de dominio público:
  SONG1: "Oda a la Alegría" (L. van Beethoven, 9.a sinfonía, 1824).
  SONG2: "Korobeiniki" (canción tradicional rusa, s. XIX).

Para cada nota:
    t_ms  = leadin + (tiempos acumulados) * 60000 / BPM
    dur   = 0,9 * tiempos * 60000 / BPM            (articulación: 10 % de silencio)
    freq  = 440 * 2^((midi - 69) / 12)             redondeada a Hz

Asignación de flechas (estilo DDR, sigue el contorno melódico):
    la altura sube  -> ARRIBA (1)         la altura baja -> ABAJO (2)
    se repite       -> alterna IZQ (0) / DER (3)
Reglas de jugabilidad (hay que volver al centro entre dos golpes del mismo carril):
    - a lo más 2 notas seguidas en el mismo carril: la tercera pasa a un carril
      horizontal (ARRIBA -> DER, ABAJO -> IZQ, IZQ <-> DER);
    - si dos notas seguidas del mismo carril quedan a menos de MIN_SAME_LANE_MS, la
      segunda se mueve igual.

Formato del archivo: ver sw/src/sd_manager_formats.c.

Uso:  python tools/make_chart.py          (escribe sd_card/SONG1.SHC y sd_card/SONG2.SHC)
"""
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
IZQ, ARRIBA, ABAJO, DER = 0, 1, 2, 3
MIN_SAME_LANE_MS = 250

SEMITONE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(name):
    """'E4', 'F#5', 'Bb4' -> número MIDI. 'R' = silencio (None)."""
    if name == "R":
        return None
    letter, rest = name[0], name[1:]
    acc = 0
    if rest[0] == "#":
        acc, rest = 1, rest[1:]
    elif rest[0] == "b":
        acc, rest = -1, rest[1:]
    return 12 * (int(rest) + 1) + SEMITONE[letter] + acc


def freq(m):
    return int(round(440.0 * 2.0 ** ((m - 69) / 12.0)))


# ---------------------------------------------------------------------------
# Melodías: (nota, tiempos). Negra = 1.
# ---------------------------------------------------------------------------
ODA = [
    # Frase A
    ("E4", 1), ("E4", 1), ("F4", 1), ("G4", 1),  ("G4", 1), ("F4", 1), ("E4", 1), ("D4", 1),
    ("C4", 1), ("C4", 1), ("D4", 1), ("E4", 1),  ("E4", 1.5), ("D4", 0.5), ("D4", 2),
    # Frase A'
    ("E4", 1), ("E4", 1), ("F4", 1), ("G4", 1),  ("G4", 1), ("F4", 1), ("E4", 1), ("D4", 1),
    ("C4", 1), ("C4", 1), ("D4", 1), ("E4", 1),  ("D4", 1.5), ("C4", 0.5), ("C4", 2),
    # Frase B
    ("D4", 1), ("D4", 1), ("E4", 1), ("C4", 1),  ("D4", 1), ("E4", 0.5), ("F4", 0.5), ("E4", 1), ("C4", 1),
    ("D4", 1), ("E4", 0.5), ("F4", 0.5), ("E4", 1), ("D4", 1),  ("C4", 1), ("D4", 1), ("G3", 2),
    # Frase A'
    ("E4", 1), ("E4", 1), ("F4", 1), ("G4", 1),  ("G4", 1), ("F4", 1), ("E4", 1), ("D4", 1),
    ("C4", 1), ("C4", 1), ("D4", 1), ("E4", 1),  ("D4", 1.5), ("C4", 0.5), ("C4", 2),
]

KOROBEINIKI_A = [
    ("E5", 1), ("B4", 0.5), ("C5", 0.5), ("D5", 1), ("C5", 0.5), ("B4", 0.5),
    ("A4", 1), ("A4", 0.5), ("C5", 0.5), ("E5", 1), ("D5", 0.5), ("C5", 0.5),
    ("B4", 1.5), ("C5", 0.5), ("D5", 1), ("E5", 1),
    ("C5", 1), ("A4", 1), ("A4", 2),
]
KOROBEINIKI_B = [
    ("R", 0.5), ("D5", 1), ("F5", 0.5), ("A5", 1), ("G5", 0.5), ("F5", 0.5),
    ("E5", 1.5), ("C5", 0.5), ("E5", 1), ("D5", 0.5), ("C5", 0.5),
    ("B4", 1), ("B4", 0.5), ("C5", 0.5), ("D5", 1), ("E5", 1),
    ("C5", 1), ("A4", 1), ("A4", 1), ("R", 1),
]
KOROBEINIKI = KOROBEINIKI_A + KOROBEINIKI_B + KOROBEINIKI_A + KOROBEINIKI_B

SONGS = [
    # archivo,      nombre (<= 15 car.),  BPM, transposición, melodía
    ("SONG1.SHC", "Oda Alegria",      120, 24, ODA),
    ("SONG2.SHC", "Korobeiniki",      140, 12, KOROBEINIKI),
]


def assign_lanes(notes):
    """notes: lista de (t_ms, midi). Devuelve la lista de carriles."""
    lanes = []
    side = DER                       # próximo carril horizontal para notas repetidas
    for i, (t, m) in enumerate(notes):
        if i == 0:
            lane = IZQ               # la primera nota va a la izquierda
        else:
            pm = notes[i - 1][1]
            if m > pm:
                lane = ARRIBA
            elif m < pm:
                lane = ABAJO
            else:
                lane = side
                side = DER if side == IZQ else IZQ
        # Reglas de jugabilidad.
        same_run = len(lanes) >= 2 and lanes[-1] == lane and lanes[-2] == lane
        too_close = len(lanes) >= 1 and lanes[-1] == lane and (t - notes[i - 1][0]) < MIN_SAME_LANE_MS
        if same_run or too_close:
            lane = {ARRIBA: DER, ABAJO: IZQ, IZQ: DER, DER: IZQ}[lane]
            if len(lanes) >= 1 and lanes[-1] == lane and (t - notes[i - 1][0]) < MIN_SAME_LANE_MS:
                lane = {DER: IZQ, IZQ: DER}[lane]
        lanes.append(lane)
    return lanes


def build(name, bpm, transpose, melody, leadin=3000):
    beat_ms = 60000.0 / bpm
    notes, rows = [], []
    beats = 0.0
    for nm, b in melody:
        m = midi(nm)
        if m is not None:
            t = int(round(leadin + beats * beat_ms))
            notes.append((t, m + transpose, int(round(0.9 * b * beat_ms))))
        beats += b
    lanes = assign_lanes([(t, m) for (t, m, _) in notes])
    for (t, m, d), lane in zip(notes, lanes):
        rows.append((t, lane, freq(m), d))
    out = ["# SEP HERO chart v1",
           "# Generado por tools/make_chart.py (no editar a mano; editar la melodia alla)",
           "NAME=%s" % name[:15],
           "BPM=%d" % bpm,
           "# t_ms  lane(0=IZQ 1=ARRIBA 2=ABAJO 3=DER)  freq_hz  dur_ms"]
    out += ["%d %d %d %d" % r for r in rows]
    return "\n".join(out) + "\n", rows


if __name__ == "__main__":
    os.makedirs(os.path.join(ROOT, "sd_card"), exist_ok=True)
    for fname, name, bpm, tr, mel in SONGS:
        text, rows = build(name, bpm, tr, mel)
        with open(os.path.join(ROOT, "sd_card", fname), "w", newline="\n") as f:
            f.write(text)
        hist = [sum(1 for r in rows if r[1] == k) for k in range(4)]
        fmin, fmax = min(r[2] for r in rows), max(r[2] for r in rows)
        print("%s: %d notas, %.1f s, carriles IZQ/ARRIBA/ABAJO/DER = %s, %d-%d Hz"
              % (fname, len(rows), (rows[-1][0] + rows[-1][3]) / 1000.0, hist, fmin, fmax))
