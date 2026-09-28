// tv_decoder.ts -- decodifica vectores "T,H,t;dT,dH,dt;..." del comando
// `tv <since>` de MeshCore (ver docs/Spec telemetría vectorial MeshCore
// (XIAO nRF52).md). Sin dependencias externas, solo el runtime de JS/TS.
//
// H es %RH si el nodo detectó un BME280, o presión atmosférica escalada
// (hPa - 800) si detectó un BMP280 -- confirmar con el comando `tv sensor`
// del nodo antes de interpretar el campo (ver README_TV_TELEMETRY.md).

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

function main() {
  const vector = "302,104,29843589;1,0,6;2,0,21;-3,-1,36;-7,-1,51";
  const samples = decodeVector(vector);

  const row = (a: string, b: string, c: string, d: string, e: string) =>
    `${a.padStart(2)}  ${b.padStart(7)}  ${c.padStart(7)}  ${d.padStart(10)}  ${e}`;

  console.log(row("#", "T (°C)", "H (raw)", "epoch_min", "UTC"));
  console.log("-".repeat(53));
  samples.forEach((s, i) => {
    console.log(row(
      String(i),
      s.tempC.toFixed(1),
      String(s.hRaw),
      String(s.epochMin),
      s.date.toISOString().replace("T", " ").replace(".000Z", "Z"),
    ));
  });

  // self-check
  console.assert(samples.length === 5, "esperaba 5 registros");
  console.assert(samples[0].tempC === 30.2 && samples[0].hRaw === 104, "anchor mal decodificado");
  console.assert(samples[4].tempC === 29.5 && samples[4].hRaw === 103, "último registro mal decodificado");
  console.assert(decodeVector("-").length === 0, "'-' debe decodificar a []");
  console.log("\nOK: self-check passed");
}

if (import.meta.url === `file://${process.argv[1]}`) main();
