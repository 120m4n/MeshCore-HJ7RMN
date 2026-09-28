"""tv_decoder.py -- decodifica vectores "T,H,t;dT,dH,dt;..." del comando
`tv <since>` de MeshCore (ver docs/Spec telemetría vectorial MeshCore
(XIAO nRF52).md). Solo stdlib, sin dependencias externas.

H es %RH si el nodo detectó un BME280, o presión atmosférica escalada
(hPa - 800) si detectó un BMP280 -- confirmar con el comando `tv sensor`
del nodo antes de interpretar el campo (ver README_TV_TELEMETRY.md).
Usar --tv_hr_adjust para des-escalar esa columna a hPa reales cuando
corresponda (ver `main()` / `--help`).
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone

class TvParseError(ValueError):
    pass

@dataclass
class TvSample:
    epoch_min: int
    date: datetime
    temp_c: float
    h_raw: int  # %RH (BME280) o hPa-800 (BMP280)

def decode_vector(s: str) -> list[TvSample]:
    v = s.strip()
    if v in ("", "-"):
        return []

    records: list[tuple[int, int, int]] = []
    for i, r in enumerate(v.split(";")):
        fields = r.split(",")
        if len(fields) != 3:
            raise TvParseError(f"registro {i} inválido: {r!r}")
        try:
            records.append(tuple(int(f) for f in fields))  # type: ignore[arg-type]
        except ValueError:
            raise TvParseError(f"registro {i} inválido: {r!r}")

    t0, h0, e0 = records[0]  # anchor
    prev_e = 0
    out: list[TvSample] = []
    for i, (t, h, e) in enumerate(records):
        T = t if i == 0 else t0 + t
        H = h if i == 0 else h0 + h
        E = e if i == 0 else e0 + e
        if not (0 <= H <= 255):
            raise TvParseError(f"registro {i}: H fuera de rango ({H})")
        if E <= prev_e:
            raise TvParseError(f"registro {i}: tiempo no creciente")
        prev_e = E
        date = datetime.fromtimestamp(E * 60, tz=timezone.utc)
        out.append(TvSample(E, date, T / 10, H))
    return out

def main() -> None:
    parser = argparse.ArgumentParser(description="Decodifica un vector tv de MeshCore.")
    parser.add_argument("vector", help='vector tv, ej. "330,65,29840113;-12,3,61"')
    parser.add_argument(
        "--tv_hr_adjust", nargs="?", type=int, const=800, default=None, metavar="OFFSET",
        help="el nodo tiene BMP280 (sin humedad): el campo H trae presión escalada "
             "(hPa - OFFSET). Suma OFFSET de vuelta para mostrar hPa reales en vez del "
             "H crudo. Sin valor, usa 800 (el offset del firmware, ver tv_sensor.cpp).",
    )
    args = parser.parse_args()

    samples = decode_vector(args.vector)

    hr_label = "Presión (hPa)" if args.tv_hr_adjust is not None else "H (raw)"
    header = f"{'#':>2}  {'T (°C)':>7}  {hr_label:>13}  {'epoch_min':>10}  {'UTC':<19}"
    print(header)
    print("-" * len(header))
    for i, s in enumerate(samples):
        hr_value = s.h_raw + args.tv_hr_adjust if args.tv_hr_adjust is not None else s.h_raw
        print(f"{i:>2}  {s.temp_c:>7.1f}  {hr_value:>13}  {s.epoch_min:>10}  "
              f"{s.date.strftime('%Y-%m-%d %H:%M:%S')}")

if __name__ == "__main__":
    main()
