// popup.ts -- UI mínima sobre decodeVector() de tools/tv_decoder/tv_decoder.ts.
import { decodeVector, TvParseError, TvSample } from "../tv_decoder/tv_decoder";
import { renderCharts } from "./chart";

const BMP_OFFSET_HPA = 800;
const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

// Una línea = una página. Tolera prefijos tipo "-> ": toma el token más largo
// del alfabeto del vector (base64url + "." y "~").
const vectorOf = (line: string) =>
  (line.match(/[A-Za-z0-9_.~-]+/g) ?? []).reduce((a, b) => (b.length > a.length ? b : a), "");

let rows: string[][] = [];
let header: string[] = [];

function decode() {
  const err = $("err"), tbl = $<HTMLTableElement>("tbl");
  err.textContent = "";
  rows = [];
  tbl.hidden = true;
  $("next").textContent = "";
  $("charts").replaceChildren();
  $<HTMLButtonElement>("csv").disabled = true;

  const samples: TvSample[] = [];
  let kind = "";
  const lines = $<HTMLTextAreaElement>("in").value.split("\n").filter((l) => l.trim());
  try {
    lines.forEach((line, i) => {
      const r = decodeVector(vectorOf(line));
      kind ||= r.kind;
      if (r.kind && r.kind !== kind) throw new TvParseError(`línea ${i + 1}: sensor ${r.kind} distinto de ${kind}`);
      samples.push(...r.samples);
    });
  } catch (e) {
    if (!(e instanceof TvParseError)) throw e;
    err.textContent = e.message;
    return;
  }
  $("info").textContent = samples.length ? `sensor: ${kind}  registros: ${samples.length}` : "sin datos";
  if (!samples.length) return;

  const bmp = kind === "BMP280", local = $<HTMLInputElement>("local").checked;
  header = ["#", "T (°C)", bmp ? "Presión (hPa)" : "%RH", "epoch_min", local ? "Local" : "UTC"];
  rows = samples.map((s, i) => [
    String(i),
    s.tempC.toFixed(1),
    String(bmp ? s.hRaw + BMP_OFFSET_HPA : s.hRaw),
    String(s.epochMin),
    local
      ? s.date.toLocaleString("sv-SE")
      : s.date.toISOString().replace("T", " ").replace(".000Z", "Z"),
  ]);
  tbl.tHead!.innerHTML = "<tr>" + header.map((h) => `<th>${h}</th>`).join("") + "</tr>";
  tbl.tBodies[0].innerHTML = rows.map((r) => "<tr>" + r.map((c) => `<td>${c}</td>`).join("") + "</tr>").join("");
  tbl.hidden = false;
  $<HTMLButtonElement>("csv").disabled = false;
  const hVal = (s: TvSample) => (bmp ? s.hRaw + BMP_OFFSET_HPA : s.hRaw);
  renderCharts($("charts"), [
    { title: "Temperatura (°C)", pts: samples.map((s) => ({ x: s.epochMin, y: s.tempC })) },
    { title: bmp ? "Presión (hPa)" : "Humedad (%RH)", pts: samples.map((s) => ({ x: s.epochMin, y: hVal(s) })) },
  ], local ? -new Date().getTimezoneOffset() : 0);
  $("next").textContent = `siguiente: tv ${samples[samples.length - 1].epochMin}`;
}

$("go").addEventListener("click", decode);
$("local").addEventListener("change", decode);
$("csv").addEventListener("click", () =>
  navigator.clipboard.writeText([header, ...rows].map((r) => r.join(",")).join("\n")),
);
