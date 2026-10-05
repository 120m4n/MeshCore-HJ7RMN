"""tv_decoder.py -- decodifica vectores v1 del comando `tv <since>` de
MeshCore (formato compacto base64url, ver encode() en
src/helpers/tv_telemetry.h y README_TV_TELEMETRY.md). Solo stdlib.

El header del vector dice qué sensor tiene el nodo: con BME280 la columna H
es %RH; con BMP280 es presión escalada (hPa - 800) y se muestra en hPa.

Vectores del formato viejo ("T,H,t;dT,dH,dt;...") se rechazan: usar el
decoder anterior desde git (ver V0_HINT).
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone

B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
IDX = {c: i for i, c in enumerate(B64)}
INTERVAL_MIN = 30
FORMAT_VERSION = 1
KINDS = {1: "BME280", 2: "BMP280"}
BMP_OFFSET_HPA = 800  # mismo offset que read_hum_pct() en tv_sensor.cpp
V0_HINT = ("vector en formato viejo v0 (\"T,H,t;...\"): decodificalo con el decoder "
           "anterior de git: git show tv-v0:tools/tv_decoder/tv_decoder.py > tv_decoder_v0.py")

class TvParseError(ValueError):
    pass

@dataclass
class TvSample:
    epoch_min: int
    date: datetime
    temp_c: float
    h_raw: int  # %RH (BME280) o hPa-800 (BMP280)

def unzigzag(u: int) -> int:
    return (u >> 1) ^ -(u & 1)

def decode_vector(s: str) -> tuple[str, list[TvSample]]:
    """Devuelve (sensor, muestras). sensor es "BME280", "BMP280" o "" si vacío."""
    v = s.strip()
    if v in ("", "-"):
        return "", []
    if "," in v:
        raise TvParseError(V0_HINT)

    pos = 0

    def take(n: int) -> int:
        nonlocal pos
        chunk = v[pos:pos + n]
        if len(chunk) < n:
            raise TvParseError("vector truncado")
        pos += n
        val = 0
        for c in chunk:
            if c not in IDX:
                raise TvParseError(f"carácter inválido {c!r} en posición {pos - n}")
            val = (val << 6) | IDX[c]
        return val

    head = take(1)
    if head >> 2 != FORMAT_VERSION or (head & 3) not in KINDS:
        raise TvParseError(f"header desconocido {v[0]!r} (versión {head >> 2})")
    kind = KINDS[head & 3]
    count = take(2)
    e0 = take(5)
    t, h = unzigzag(take(2)), take(2)
    recs = [(e0, t, h)]
    slot = e0 // INTERVAL_MIN

    while pos < len(v):
        c = v[pos]
        if c == ".":
            pos += 1
            slot += take(1)
            continue
        if c in "~!":
            pos += 1
            t, h = unzigzag(take(2)), take(2)
        else:
            t += unzigzag(take(1))
            h += unzigzag(take(1))
        slot += 1
        recs.append((slot * INTERVAL_MIN, t, h))

    if len(recs) != count:
        raise TvParseError(f"se esperaban {count} registros y llegaron {len(recs)}: vector truncado")

    out: list[TvSample] = []
    for i, (e, t, h) in enumerate(recs):
        if not 0 <= h <= 255:
            raise TvParseError(f"registro {i}: H fuera de rango ({h})")
        out.append(TvSample(e, datetime.fromtimestamp(e * 60, tz=timezone.utc), t / 10, h))
    return kind, out

def main() -> None:
    parser = argparse.ArgumentParser(description="Decodifica un vector tv (v1) de MeshCore.")
    parser.add_argument("vector", help='vector tv, ej. "FAFBx1KKIuA3CCCBDC.C!BFA8"')
    args = parser.parse_args()

    try:
        kind, samples = decode_vector(args.vector)
    except TvParseError as e:
        parser.exit(1, f"error: {e}\n")

    bmp = kind == "BMP280"
    hr_label = "Presión (hPa)" if bmp else "%RH"
    print(f"sensor: {kind or '-'}  registros: {len(samples)}")
    header = f"{'#':>2}  {'T (°C)':>7}  {hr_label:>13}  {'epoch_min':>10}  {'UTC':<19}"
    print(header)
    print("-" * len(header))
    for i, s in enumerate(samples):
        hr_value = s.h_raw + BMP_OFFSET_HPA if bmp else s.h_raw
        print(f"{i:>2}  {s.temp_c:>7.1f}  {hr_value:>13}  {s.epoch_min:>10}  "
              f"{s.date.strftime('%Y-%m-%d %H:%M:%S')}")

if __name__ == "__main__":
    main()
