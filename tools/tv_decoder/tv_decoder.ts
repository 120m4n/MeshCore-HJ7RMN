// tv_decoder.ts -- decodifica vectores "T,H,t;dT,dH,dt;..." del comando
// `tv <since>` de MeshCore (ver docs/Spec telemetría vectorial MeshCore
// (XIAO nRF52).md). Sin dependencias externas, solo el runtime de JS/TS.
//
// H es %RH si el nodo detectó un BME280, o presión atmosférica escalada
// (hPa - 800) si detectó un BMP280 -- confirmar con el comando `tv sensor`
// del nodo antes de interpretar el campo (ver README_TV_TELEMETRY.md).
// Usar --tv_hr_adjust para des-escalar esa columna a hPa reales cuando
// corresponda (ver printHelp() / --help).

export interface TvSample {
  epochMin: number; // minutos desde epoch, UTC
  date: Date;        // el mismo instante como Date
  tempC: number;      // °C con 1 decimal
  hRaw: number;        // %RH (BME280) o hPa-800 (BMP280)
}

export class TvParseError extends Error {}

const INT = /^-?\d+$/;

export function decodeVector(s: string): TvSample[] {
  const v = s.trim();
  if (v === "" || v === "-") return [];

  const recs = v.split(";").map((r, i) => {
    const f = r.split(",");
    if (f.length !== 3 || !f.every((x) => INT.test(x))) {
      throw new TvParseError(`registro ${i} inválido: "${r}"`);
    }
    return f.map(Number) as [number, number, number];
  });

  const [t0, h0, e0] = recs[0]; // anchor
  let prevE = 0;

  return recs.map(([t, h, e], i) => {
    const T = i === 0 ? t : t0 + t;
    const H = i === 0 ? h : h0 + h;
    const E = i === 0 ? e : e0 + e;
    if (H < 0 || H > 255) throw new TvParseError(`registro ${i}: H fuera de rango (${H})`);
    if (E <= prevE) throw new TvParseError(`registro ${i}: tiempo no creciente`);
    prevE = E;
    return { epochMin: E, date: new Date(E * 60_000), tempC: T / 10, hRaw: H };
  });
}

function printHelp() {
  console.log(
    'Uso: node tv_decoder.ts "<vector>" [--tv_hr_adjust[=OFFSET]]\n\n' +
    '  <vector>            vector tv, ej. "330,65,29840113;-12,3,61"\n' +
    "  --tv_hr_adjust      el nodo tiene BMP280 (sin humedad): el campo H trae\n" +
    "                      presión escalada (hPa - OFFSET). Suma OFFSET de vuelta\n" +
    "                      para mostrar hPa reales en vez del H crudo. Sin valor,\n" +
    "                      usa 800 (el offset del firmware, ver tv_sensor.cpp).",
  );
}

function parseArgs(argv: string[]): { vector: string; hrAdjust: number | null } {
  let vector: string | null = null;
  let hrAdjust: number | null = null;

  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "-h" || a === "--help") {
      printHelp();
      process.exit(0);
    } else if (a.startsWith("--tv_hr_adjust=")) {
      hrAdjust = Number(a.slice("--tv_hr_adjust=".length));
    } else if (a === "--tv_hr_adjust") {
      const next = argv[i + 1];
      if (next !== undefined && /^-?\d+$/.test(next)) {
        hrAdjust = Number(next);
        i++;
      } else {
        hrAdjust = 800;
      }
    } else if (vector === null) {
      vector = a;
    }
  }

  if (vector === null) {
    printHelp();
    process.exit(1);
  }
  return { vector, hrAdjust };
}

function main() {
  const { vector, hrAdjust } = parseArgs(process.argv.slice(2));
  const samples = decodeVector(vector);

  const hrLabel = hrAdjust !== null ? "Presión (hPa)" : "H (raw)";
  const row = (a: string, b: string, c: string, d: string, e: string) =>
    `${a.padStart(2)}  ${b.padStart(7)}  ${c.padStart(13)}  ${d.padStart(10)}  ${e}`;

  console.log(row("#", "T (°C)", hrLabel, "epoch_min", "UTC"));
  console.log("-".repeat(59));
  samples.forEach((s, i) => {
    const hrValue = hrAdjust !== null ? s.hRaw + hrAdjust : s.hRaw;
    console.log(row(
      String(i),
      s.tempC.toFixed(1),
      String(hrValue),
      String(s.epochMin),
      s.date.toISOString().replace("T", " ").replace(".000Z", "Z"),
    ));
  });
}

if (import.meta.url === `file://${process.argv[1]}`) main();
