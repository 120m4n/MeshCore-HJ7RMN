(() => {
  // <define:process.argv>
  var define_process_argv_default = [];

  // ../tv_decoder/tv_decoder.ts
  var B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  var INTERVAL_MIN = 30;
  var FORMAT_VERSION = 1;
  var KINDS = { 1: "BME280", 2: "BMP280" };
  var BMP_OFFSET_HPA = 800;
  var V0_HINT = 'vector en formato viejo v0 ("T,H,t;..."): decodificalo con el decoder anterior de git: git show tv-v0:tools/tv_decoder/tv_decoder.ts > tv_decoder_v0.ts';
  var TvParseError = class extends Error {
  };
  var unzigzag = (u) => u >>> 1 ^ -(u & 1);
  function decodeVector(s) {
    const v = s.trim();
    if (v === "" || v === "-") return { kind: "", samples: [] };
    if (v.includes(",")) throw new TvParseError(V0_HINT);
    let pos = 0;
    const take = (n) => {
      if (pos + n > v.length) throw new TvParseError("vector truncado");
      let val = 0;
      for (let i = 0; i < n; i++) {
        const d = B64.indexOf(v[pos + i]);
        if (d < 0) throw new TvParseError(`car\xE1cter inv\xE1lido "${v[pos + i]}" en posici\xF3n ${pos + i}`);
        val = val * 64 + d;
      }
      pos += n;
      return val;
    };
    const head = take(1);
    const kind = KINDS[head & 3];
    if (head >> 2 !== FORMAT_VERSION || !kind) {
      throw new TvParseError(`header desconocido "${v[0]}" (versi\xF3n ${head >> 2})`);
    }
    const count = take(2);
    const e0 = take(5);
    let t = unzigzag(take(2));
    let h = take(2);
    const recs = [[e0, t, h]];
    let slot = Math.floor(e0 / INTERVAL_MIN);
    while (pos < v.length) {
      const c = v[pos];
      if (c === ".") {
        pos++;
        slot += take(1);
        continue;
      }
      if (c === "~") {
        pos++;
        t = unzigzag(take(2));
        h = take(2);
      } else {
        t += unzigzag(take(1));
        h += unzigzag(take(1));
      }
      slot++;
      recs.push([slot * INTERVAL_MIN, t, h]);
    }
    if (recs.length !== count) {
      throw new TvParseError(`se esperaban ${count} registros y llegaron ${recs.length}: vector truncado`);
    }
    const samples = recs.map(([e, t2, h2], i) => {
      if (h2 < 0 || h2 > 255) throw new TvParseError(`registro ${i}: H fuera de rango (${h2})`);
      return { epochMin: e, date: new Date(e * 6e4), tempC: t2 / 10, hRaw: h2 };
    });
    return { kind, samples };
  }
  function main() {
    const vector = define_process_argv_default[2];
    if (vector === void 0 || vector === "-h" || vector === "--help") {
      console.log("Uso: node tv_decoder.ts '<vector>'   (ej. 'FAFBx1KKIuA3CCCBDC.C~BFA8')");
      process.exit(vector === void 0 ? 1 : 0);
    }
    let res;
    try {
      res = decodeVector(vector);
    } catch (e) {
      if (!(e instanceof TvParseError)) throw e;
      console.error(`error: ${e.message}`);
      process.exit(1);
    }
    const bmp = res.kind === "BMP280";
    const row = (a, b, c, d, e) => `${a.padStart(2)}  ${b.padStart(7)}  ${c.padStart(13)}  ${d.padStart(10)}  ${e}`;
    console.log(`sensor: ${res.kind || "-"}  registros: ${res.samples.length}`);
    console.log(row("#", "T (\xB0C)", bmp ? "Presi\xF3n (hPa)" : "%RH", "epoch_min", "UTC"));
    console.log("-".repeat(59));
    res.samples.forEach((s, i) => {
      console.log(row(
        String(i),
        s.tempC.toFixed(1),
        String(bmp ? s.hRaw + BMP_OFFSET_HPA : s.hRaw),
        String(s.epochMin),
        s.date.toISOString().replace("T", " ").replace(".000Z", "Z")
      ));
    });
  }
  if ("" === `file://${define_process_argv_default[1]}`) main();

  // popup.ts
  var BMP_OFFSET_HPA2 = 800;
  var $ = (id) => document.getElementById(id);
  var vectorOf = (line) => (line.match(/[A-Za-z0-9_.~-]+/g) ?? []).reduce((a, b) => b.length > a.length ? b : a, "");
  var rows = [];
  var header = [];
  function decode() {
    const err = $("err"), tbl = $("tbl");
    err.textContent = "";
    rows = [];
    tbl.hidden = true;
    $("next").textContent = "";
    $("csv").disabled = true;
    const samples = [];
    let kind = "";
    const lines = $("in").value.split("\n").filter((l) => l.trim());
    try {
      lines.forEach((line, i) => {
        const r = decodeVector(vectorOf(line));
        kind ||= r.kind;
        if (r.kind && r.kind !== kind) throw new TvParseError(`l\xEDnea ${i + 1}: sensor ${r.kind} distinto de ${kind}`);
        samples.push(...r.samples);
      });
    } catch (e) {
      if (!(e instanceof TvParseError)) throw e;
      err.textContent = e.message;
      return;
    }
    $("info").textContent = samples.length ? `sensor: ${kind}  registros: ${samples.length}` : "sin datos";
    if (!samples.length) return;
    const bmp = kind === "BMP280", local = $("local").checked;
    header = ["#", "T (\xB0C)", bmp ? "Presi\xF3n (hPa)" : "%RH", "epoch_min", local ? "Local" : "UTC"];
    rows = samples.map((s, i) => [
      String(i),
      s.tempC.toFixed(1),
      String(bmp ? s.hRaw + BMP_OFFSET_HPA2 : s.hRaw),
      String(s.epochMin),
      local ? s.date.toLocaleString("sv-SE") : s.date.toISOString().replace("T", " ").replace(".000Z", "Z")
    ]);
    tbl.tHead.innerHTML = "<tr>" + header.map((h) => `<th>${h}</th>`).join("") + "</tr>";
    tbl.tBodies[0].innerHTML = rows.map((r) => "<tr>" + r.map((c) => `<td>${c}</td>`).join("") + "</tr>").join("");
    tbl.hidden = false;
    $("csv").disabled = false;
    $("next").textContent = `siguiente: tv ${samples[samples.length - 1].epochMin}`;
  }
  $("go").addEventListener("click", decode);
  $("local").addEventListener("change", decode);
  $("csv").addEventListener(
    "click",
    () => navigator.clipboard.writeText([header, ...rows].map((r) => r.join(",")).join("\n"))
  );
})();
