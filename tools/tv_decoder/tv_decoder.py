#!/usr/bin/env python3
"""tv_decoder.py -- decodifica vectores "T,H,t;dT,dH,dt;..." del comando
`tv <since>` de MeshCore (ver docs/Spec telemetría vectorial MeshCore
(XIAO nRF52).md). Solo stdlib, sin dependencias externas.

H es %RH si el nodo detectó un BME280, o presión atmosférica escalada
(hPa - 800) si detectó un BMP280 -- confirmar con el comando `tv sensor`
del nodo antes de interpretar el campo (ver README_TV_TELEMETRY.md).
"""
from __future__ import annotations

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


if __name__ == "__main__":
    vector = "302,104,29843589;1,0,6;2,0,21;-3,-1,36;-7,-1,51"
    samples = decode_vector(vector)

    header = f"{'#':>2}  {'T (°C)':>7}  {'H (raw)':>7}  {'epoch_min':>10}  {'UTC':<19}"
    print(header)
    print("-" * len(header))
    for i, s in enumerate(samples):
        print(f"{i:>2}  {s.temp_c:>7.1f}  {s.h_raw:>7}  {s.epoch_min:>10}  "
              f"{s.date.strftime('%Y-%m-%d %H:%M:%S')}")

    # self-check
    assert len(samples) == 5
    assert samples[0].temp_c == 30.2 and samples[0].h_raw == 104
    assert samples[-1].temp_c == 29.5 and samples[-1].h_raw == 103
    assert decode_vector("-") == []
    print("\nOK: self-check passed")
