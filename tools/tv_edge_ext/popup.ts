// popup.ts -- UI mínima sobre decodeVector() de tools/tv_decoder/tv_decoder.ts.
import { decodeVector, TvParseError, TvSample } from "../tv_decoder/tv_decoder";
import { renderCharts } from "./chart";

const BMP_OFFSET_HPA = 800;
const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

// Una línea = una página. Tolera prefijos tipo "-> ": toma el token más largo
// del alfabeto del vector (base64url + "." y "~").
const vectorOf = (line: string) =>
  (line.match(/[A-Za-z0-9_.~-]+/g) ?? []).reduce((a, b) => (b.length > a.length ? b : a), "");

let showCharts = true;
let rows: string[][] = [];
let header: string[] = [];

// Muestra u oculta un banner (err/warn) con su texto.
const banner = (id: string, text = "") => {
  $(id).textContent = text;
  $(id).hidden = !text;
};

function copy(text: string, btn: HTMLElement) {
  navigator.clipboard.writeText(text);
  const label = btn.textContent;
  btn.textContent = "Copiado ✓";
  setTimeout(() => (btn.textContent = label), 1200);
}

function decode() {
  const tbl = $<HTMLTableElement>("tbl");
  banner("err");
  banner("warn");
  $("empty").hidden = true;
  $("info").hidden = true;
  rows = [];
  $("tblwrap").hidden = true;
  $("nextbox").hidden = true;
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
    banner("err", e.message);
    return;
  }
  $("info").textContent = samples.length ? `${kind} · ${samples.length} registros` : "sin datos";
  $("info").hidden = false;
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
  $("tblwrap").hidden = false;
  $<HTMLButtonElement>("csv").disabled = false;
  const hVal = (s: TvSample) => (bmp ? s.hRaw + BMP_OFFSET_HPA : s.hRaw);
  if (showCharts && samples.length < 2) {
    banner("warn", "Se necesitan al menos 2 puntos para graficar.");
  } else if (showCharts) renderCharts($("charts"), [
    { title: "Temperatura (°C)", pts: samples.map((s) => ({ x: s.epochMin, y: s.tempC })) },
    { title: bmp ? "Presión (hPa)" : "Humedad (%RH)", pts: samples.map((s) => ({ x: s.epochMin, y: hVal(s) })) },
  ], local ? -new Date().getTimezoneOffset() : 0);
  $("next").textContent = `tv ${samples[samples.length - 1].epochMin}`;
  $("nextbox").hidden = false;
}

$("go").addEventListener("click", decode);
$("toggle").addEventListener("click", () => {
  showCharts = !showCharts;
  $("toggle").textContent = `Gráficas: ${showCharts ? "on" : "off"}`;
  $("toggle").setAttribute("aria-pressed", String(showCharts));
  decode();
});
$("local").addEventListener("change", decode);
$("csv").addEventListener("click", () =>
  copy([header, ...rows].map((r) => r.join(",")).join("\n"), $("csv")),
);
$("copynext").addEventListener("click", () => copy($("next").textContent!, $("copynext")));
