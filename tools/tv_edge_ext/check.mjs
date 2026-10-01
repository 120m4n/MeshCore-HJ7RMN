// Compara decodeVector (TS) con la salida de tv_decoder.py. Uso: node tools/tv_edge_ext/check.mjs
import { execFileSync } from "node:child_process";
import { decodeVector } from "../tv_decoder/tv_decoder.ts";

const v = "FAFBx1KKIuA3CCCBDC.C~BFA8";
const { kind, samples } = decodeVector(v);
const py = execFileSync("python3", [new URL("../tv_decoder/tv_decoder.py", import.meta.url).pathname, v], { encoding: "utf8" });
const pyEpochs = [...py.matchAll(/^\s*\d+\s+\S+\s+\S+\s+(\d+)\s/gm)].map((m) => +m[1]);
if (kind !== "BME280" || JSON.stringify(samples.map((s) => s.epochMin)) !== JSON.stringify(pyEpochs)) {
  console.error("FAIL", kind, samples, pyEpochs);
  process.exit(1);
}
console.log(`ok: ${samples.length} registros coinciden con tv_decoder.py`);
