// Compara decodeVector (TS) con la salida de tv_decoder.py. Uso: node tools/tv_edge_ext/check.mjs
import { execFileSync } from "node:child_process";
import { decodeVector } from "../tv_decoder/tv_decoder.ts";

const v = "FAFBx1KKIuA3CCCBDC.C!BFA8";
const { kind, samples } = decodeVector(v);
const py = execFileSync("python3", [new URL("../tv_decoder/tv_decoder.py", import.meta.url).pathname, v], { encoding: "utf8" });
const pyEpochs = [...py.matchAll(/^\s*\d+\s+\S+\s+\S+\s+(\d+)\s/gm)].map((m) => +m[1]);
if (kind !== "BME280" || JSON.stringify(samples.map((s) => s.epochMin)) !== JSON.stringify(pyEpochs)) {
  console.error("FAIL", kind, samples, pyEpochs);
  process.exit(1);
}
console.log(`ok: ${samples.length} registros coinciden con tv_decoder.py`);

// chart.ts: funciones puras
import { mean, niceTicks, segments } from "./chart.ts";
const eq = (a, b, msg) => { if (JSON.stringify(a) !== JSON.stringify(b)) { console.error("FAIL", msg, a, b); process.exit(1); } };
const nt = niceTicks(27.9, 28.1);
eq(nt[0] <= 27.9 && nt.at(-1) >= 28.1 && nt.length <= 6, true, "niceTicks cubre el rango corto");
eq(niceTicks(0, 100), [0, 50, 100], "niceTicks 0-100");
eq(niceTicks(-3.5, 28.1), [-10, 0, 10, 20, 30], "niceTicks cruza cero");
eq(mean([1, 2, 6]), 3, "mean");
eq(segments([{ x: 0, y: 1 }, { x: 30, y: 1 }, { x: 90, y: 1 }, { x: 120, y: 1 }]).map((s) => s.length), [2, 2], "segments corta en hueco");
import { extremes } from "./chart.ts";
const ex = extremes([{ x: 0, y: 2 }, { x: 30, y: 5 }, { x: 60, y: 1 }]);
eq([ex.min.x, ex.max.x], [60, 30], "extremes mín/máx");
eq(extremes([{ x: 0, y: 3 }, { x: 30, y: 3 }]), null, "extremes serie plana");
console.log("ok: chart.ts (niceTicks, mean, segments, extremes)");

// tabs.ts: una pestaña por línea válida (vectores BMP280 reales: 1 y 20 registros)
import { parseLines } from "./tabs.ts";
const BMP1 = "GABBtFfMIUBj", BMP20 = "GAUBx23tIkBkDADABAABAACAFCBBEAAAEACBECCABACCFADAAA";
const txt = [BMP1, "", "-", "FAFBx1KKIuA3CCCB", BMP20, v].join("\n");
let p = parseLines(txt);
eq(p.tabs.map((t) => [t.line, t.samples.length, t.kind]), [[1, 1, "BMP280"], [5, 20, "BMP280"], [6, 5, "BME280"]], "tabs por línea válida");
eq([p.errors.length, p.errors[0].startsWith("línea 4:"), p.skipped], [1, true, 0], "error parcial no aborta");
p = parseLines(txt, 2);
eq([p.tabs.length, p.skipped], [2, 1], "límite de tabs");
import { mergeTabs } from "./tabs.ts";
const t20 = parseLines(BMP20).tabs[0], t1 = parseLines(BMP1).tabs[0], t5 = parseLines(v).tabs[0];
eq(mergeTabs([t20, t20]).samples.length, 20, "mergeTabs deduplica por epoch");
const m = mergeTabs([t20, t1]);
eq([m.line, m.samples.length, m.samples[0].epochMin < m.samples[1].epochMin], [0, 21, true], "mergeTabs ordena");
eq([mergeTabs([t20]), mergeTabs([t20, t5])], [null, null], "mergeTabs null: 1 pestaña o sensores mezclados");
console.log("ok: tabs.ts (parseLines, mergeTabs)");
