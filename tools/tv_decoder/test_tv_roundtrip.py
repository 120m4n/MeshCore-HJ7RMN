"""Round-trip: compila el encoder real (src/helpers/tv_telemetry.h) en host
con g++, llena el ring, pagina con `tv <since>` y decodifica con
tv_decoder.py. Uso: python3 tools/tv_decoder/test_tv_roundtrip.py
"""
import pathlib
import subprocess
import tempfile

from tv_decoder import TvParseError, decode_vector

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = r"""
#include <iostream>
#include <string>
#include "helpers/tv_telemetry.h"
namespace tv {
uint8_t kind = 1;
bool sensor_ready() { return true; }
int16_t read_temp_dC() { return 0; }
uint8_t read_hum_pct() { return 0; }
const char* sensor_kind() { return "test"; }
uint8_t sensor_kind_id() { return kind; }
}
// stdin: "k <kind>" | "put <epoch_min> <t> <h>" | "enc <now_min> <since>" (imprime vector)
int main() {
  std::string op;
  while (std::cin >> op) {
    if (op == "k") { int k; std::cin >> k; tv::kind = (uint8_t)k; }
    else if (op == "put") { unsigned long e; int t, h; std::cin >> e >> t >> h; tv::put(e, (int16_t)t, (uint8_t)h); }
    else if (op == "enc") { unsigned long now, since; std::cin >> now >> since;
      char out[150]; tv::encode(now, since, out, sizeof out); std::cout << out << "\n"; }
  }
}
"""

def build(tmp: pathlib.Path) -> pathlib.Path:
    src, exe = tmp / "h.cpp", tmp / "h"
    src.write_text(HARNESS)
    subprocess.run(["g++", "-std=c++17", "-I", str(ROOT / "src"), str(src), "-o", str(exe)], check=True)
    return exe

def run(exe, script: str) -> list[str]:
    return subprocess.run([str(exe)], input=script, capture_output=True, text=True, check=True).stdout.split()

def fetch_all(exe, puts: str, now: int, kind: int = 1):
    """Pagina como el backend: since = epoch del último registro decodificado."""
    since, pages, got = 0, [], []
    while True:
        vec = run(exe, f"k {kind}\n{puts}enc {now} {since}\n")[0]
        if vec == "-":
            return pages, got
        k, samples = decode_vector(vec)
        pages.append(vec)
        got += [(s.epoch_min, round(s.temp_c * 10), s.h_raw, k) for s in samples]
        since = samples[-1].epoch_min

def main():
    with tempfile.TemporaryDirectory() as d:
        exe = build(pathlib.Path(d))
        I = 30  # INTERVAL_MIN
        E0 = 29840010  # múltiplo de I

        # 1) día estable completo: 48 registros en 1 sola página
        puts = "".join(f"put {E0 + I * i} 280 55\n" for i in range(48))
        now = E0 + I * 47
        pages, got = fetch_all(exe, puts, now)
        assert len(pages) == 1, pages
        assert got == [(E0 + I * i, 280, 55, "BME280") for i in range(48)], got
        assert all(len(p) < 150 for p in pages)

        # 2) negativos, anchor fuera de bucket, huecos, saltos grandes, BMP280
        # ponytail: el timestamp de registros no-anchor es el inicio del bucket (pierde minutos dentro del bucket)
        # y deltas en el borde: -33/+32 van con '~', -32/+31 en 1 char
        want = [(E0 + 7, -35, 200), (E0 + I, -68, 198), (E0 + I * 3, 500, 198),
                (E0 + I * 4, 532, 198), (E0 + I * 40, 533, 10), (E0 + I * 44, -400, 255),
                (E0 + I * 45, -432, 223), (E0 + I * 46, -401, 254)]
        puts = "".join(f"put {e} {t} {h}\n" for e, t, h in want)
        pages, got = fetch_all(exe, puts, E0 + I * 46, kind=2)
        assert got == [(e, t, h, "BMP280") for e, t, h in want], got
        assert "." in pages[0] and "~" in pages[0], pages

        # 3) vacío, formato viejo y truncamiento
        assert run(exe, f"enc {now} 0\n") == ["-"]
        assert decode_vector("-") == ("", [])
        for bad, msg in [("280,55,29840000;0,0,15", "git show"), (pages[0][:-2], "truncado")]:
            try:
                decode_vector(bad)
                raise AssertionError(f"debió fallar: {bad}")
            except TvParseError as e:
                assert msg in str(e), e
    print("ok")

if __name__ == "__main__":
    main()
