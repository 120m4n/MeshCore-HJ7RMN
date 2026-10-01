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

  // tabs.ts
  var MAX_TABS = 6;
  var vectorOf = (line) => (line.match(/[A-Za-z0-9_.~-]+/g) ?? []).reduce((a, b) => b.length > a.length ? b : a, "");
  function mergeTabs(tabs2) {
    if (tabs2.length < 2 || tabs2.some((t) => t.kind !== tabs2[0].kind)) return null;
    const byEpoch = /* @__PURE__ */ new Map();
    for (const t of tabs2) for (const s of t.samples) byEpoch.set(s.epochMin, s);
    return { line: 0, kind: tabs2[0].kind, samples: [...byEpoch.values()].sort((a, b) => a.epochMin - b.epochMin) };
  }
  function parseLines(text, max = MAX_TABS) {
    const tabs2 = [], errors2 = [];
    let skipped2 = 0;
    text.split("\n").forEach((l, i) => {
      if (!l.trim()) return;
      try {
        const r = decodeVector(vectorOf(l));
        if (!r.samples.length) return;
        if (tabs2.length < max) tabs2.push({ line: i + 1, ...r });
        else skipped2++;
      } catch (e) {
        if (!(e instanceof TvParseError)) throw e;
        errors2.push(`l\xEDnea ${i + 1}: ${e.message}`);
      }
    });
    return { tabs: tabs2, errors: errors2, skipped: skipped2 };
  }

  // chart.ts
  var SLOT_MIN = 30;
  function extremes(pts) {
    const min = pts.reduce((a, b) => b.y < a.y ? b : a);
    const max = pts.reduce((a, b) => b.y > a.y ? b : a);
    return min.y === max.y ? null : { min, max };
  }
  var mean = (xs) => xs.reduce((a, b) => a + b, 0) / xs.length;
  function niceTicks(min, max, n = 4) {
    const raw = (max - min || 1) / n;
    const mag = 10 ** Math.floor(Math.log10(raw));
    const norm = raw / mag;
    const step = (norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 5 ? 5 : 10) * mag;
    const ticks = [];
    for (let v = Math.floor(min / step) * step; v < max + step - 1e-9; v += step) ticks.push(+v.toFixed(10));
    return ticks;
  }
  function segments(pts) {
    const out = [];
    pts.forEach((p, i) => {
      if (i > 0 && p.x - pts[i - 1].x <= SLOT_MIN) out[out.length - 1].push(p);
      else out.push([p]);
    });
    return out;
  }
  var fmtTime = (x, offsetMin) => new Date((x + offsetMin) * 6e4).toISOString().slice(0, 16).replace("T", " ");
  var W = 540;
  var L = 46;
  var R = 14;
  var PH = 110;
  var BLOCK = PH + 46;
  var NS = "http://www.w3.org/2000/svg";
  function el(parent, name, attrs = {}, text) {
    const e = document.createElementNS(NS, name);
    for (const k in attrs) e.setAttribute(k, String(attrs[k]));
    if (text !== void 0) e.textContent = text;
    parent.appendChild(e);
    return e;
  }
  function renderCharts(host, panels, offsetMin) {
    host.replaceChildren();
    const all = panels[0].pts;
    if (!all.length) return;
    let x0 = all[0].x, x1 = all[all.length - 1].x;
    if (x0 === x1) {
      x0 -= SLOT_MIN;
      x1 += SLOT_MIN;
    }
    const px = (x) => L + (x - x0) / (x1 - x0) * (W - L - R);
    const stepMin = [60, 120, 180, 360, 720].find((s) => (x1 - x0) / s <= 6) ?? 1440;
    const xticks = [];
    for (let t = Math.ceil((x0 + offsetMin) / stepMin) * stepMin; t <= x1 + offsetMin; t += stepMin) xticks.push(t - offsetMin);
    const svg = el(host, "svg", {
      viewBox: `0 0 ${W} ${panels.length * BLOCK}`,
      width: "100%",
      role: "img",
      "aria-label": panels.map((p) => p.title).join(" y ")
    });
    const ys = [];
    panels.forEach((p, i) => {
      const top = i * BLOCK + 18;
      const ticks = niceTicks(Math.min(...p.pts.map((q) => q.y)), Math.max(...p.pts.map((q) => q.y)));
      const lo = ticks[0], hi = ticks[ticks.length - 1];
      const py = (y) => top + PH - (y - lo) / (hi - lo) * PH;
      ys.push(py);
      el(svg, "text", { x: L, y: top - 10, class: "t-ink" }, p.title);
      for (const v of ticks) {
        el(svg, "line", { x1: L, x2: W - R, y1: py(v), y2: py(v), class: "grid" });
        el(svg, "text", { x: L - 6, y: py(v) + 3, "text-anchor": "end", class: "t-mute" }, String(v));
      }
      for (const t of xticks) {
        const d = new Date((t + offsetMin) * 6e4);
        const label = d.getUTCHours() === 0 && d.getUTCMinutes() === 0 ? fmtTime(t, offsetMin).slice(5, 10) : fmtTime(t, offsetMin).slice(11);
        el(svg, "text", { x: px(t), y: top + PH + 14, "text-anchor": "middle", class: "t-mute" }, label);
      }
      for (const seg of segments(p.pts)) {
        if (seg.length === 1) el(svg, "circle", { cx: px(seg[0].x), cy: py(seg[0].y), r: 2.5, class: "dot" });
        else el(svg, "path", { d: "M" + seg.map((q) => `${px(q.x)} ${py(q.y)}`).join("L"), class: "line" });
      }
      const ex = extremes(p.pts);
      if (ex) {
        const mark = (q, label) => {
          const x = px(q.x), side = x < (L + W - R) / 2 ? 1 : -1;
          el(svg, "circle", { cx: x, cy: py(q.y), r: 4, class: "ext" });
          el(
            svg,
            "text",
            { x: x + side * 8, y: py(q.y) + 4, "text-anchor": side > 0 ? "start" : "end", class: "t-ink halo" },
            `${label} ${q.y.toFixed(1)}`
          );
        };
        mark(ex.max, "m\xE1x");
        mark(ex.min, "m\xEDn");
      }
      const m = mean(p.pts.map((q) => q.y));
      el(svg, "line", { x1: L, x2: W - R, y1: py(m), y2: py(m), class: "mean" });
      el(svg, "text", { x: W - R, y: py(m) - 4, "text-anchor": "end", class: "t-ink halo" }, `prom ${m.toFixed(1)}`);
    });
    const cross = el(svg, "line", { y1: 0, y2: panels.length * BLOCK, class: "cross", visibility: "hidden" });
    const marks = panels.map(() => el(svg, "circle", { r: 4, class: "dot", visibility: "hidden" }));
    const tip = document.createElement("div");
    tip.className = "tip";
    tip.hidden = true;
    host.appendChild(tip);
    svg.addEventListener("pointermove", (ev) => {
      const r = svg.getBoundingClientRect(), k = r.width / W, mx = (ev.clientX - r.left) / k;
      const i = all.reduce((b, q, j) => Math.abs(px(q.x) - mx) < Math.abs(px(all[b].x) - mx) ? j : b, 0);
      const x = px(all[i].x);
      cross.setAttribute("x1", String(x));
      cross.setAttribute("x2", String(x));
      cross.setAttribute("visibility", "visible");
      marks.forEach((m, j) => {
        m.setAttribute("cx", String(x));
        m.setAttribute("cy", String(ys[j](panels[j].pts[i].y)));
        m.setAttribute("visibility", "visible");
      });
      tip.textContent = [fmtTime(all[i].x, offsetMin), ...panels.map((p) => `${p.title}: ${p.pts[i].y.toFixed(1)}`)].join("\n");
      tip.hidden = false;
      tip.style.left = `${Math.min(x * k + 10, r.width - tip.offsetWidth)}px`;
    });
    svg.addEventListener("pointerleave", () => {
      cross.setAttribute("visibility", "hidden");
      marks.forEach((m) => m.setAttribute("visibility", "hidden"));
      tip.hidden = true;
    });
  }

  // popup.ts
  var BMP_OFFSET_HPA2 = 800;
  var $ = (id) => document.getElementById(id);
  var PREFS = "tv_edge_ext.prefs";
  var loadPrefs = () => {
    try {
      return JSON.parse(localStorage.getItem(PREFS) ?? "{}");
    } catch {
      return {};
    }
  };
  var savePrefs = () => {
    try {
      localStorage.setItem(PREFS, JSON.stringify({ charts: showCharts, local: $("local").checked }));
    } catch {
    }
  };
  var showCharts = loadPrefs().charts ?? true;
  var tabs = [];
  var active = 0;
  var errors = [];
  var skipped = 0;
  var rows = [];
  var header = [];
  var banner = (id, text = "") => {
    $(id).textContent = text;
    $(id).hidden = !text;
  };
  function copy(text, btn) {
    navigator.clipboard.writeText(text);
    const label = btn.textContent;
    btn.textContent = "Copiado \u2713";
    setTimeout(() => btn.textContent = label, 1200);
  }
  function decode() {
    let lines;
    ({ tabs: lines, errors, skipped } = parseLines($("in").value));
    const all = mergeTabs(lines);
    tabs = all ? [...lines, all] : lines;
    active = 0;
    $("empty").hidden = true;
    render();
  }
  function render() {
    const bar = $("tabs");
    bar.replaceChildren();
    $("charts").replaceChildren();
    rows = [];
    $("info").hidden = true;
    $("panel").hidden = true;
    $("csv").disabled = true;
    const warns = [];
    if (skipped) warns.push(`Se muestran ${tabs.length} de ${tabs.length + skipped} l\xEDneas v\xE1lidas (m\xE1x. ${MAX_TABS}).`);
    if (!tabs.length && !errors.length) warns.push("No hay l\xEDneas v\xE1lidas con datos.");
    banner("err", errors.join("\n"));
    if (!tabs.length) return banner("warn", warns.join("\n"));
    bar.hidden = tabs.length < 2;
    tabs.forEach((t, i) => {
      const b = document.createElement("button");
      b.role = "tab";
      b.textContent = `${t.line ? `L${t.line}` : "Todas"} \xB7 ${t.samples.length} reg`;
      b.setAttribute("aria-selected", String(i === active));
      b.tabIndex = i === active ? 0 : -1;
      b.addEventListener("click", () => select(i));
      bar.appendChild(b);
    });
    const { kind, samples } = tabs[active];
    const bmp = kind === "BMP280", local = $("local").checked;
    const hVal = (h) => bmp ? h + BMP_OFFSET_HPA2 : h;
    if (showCharts && samples.length < 2) warns.push("Se necesitan al menos 2 puntos para graficar.");
    banner("warn", warns.join("\n"));
    $("info").textContent = `${kind} \xB7 ${samples.length} registro${samples.length === 1 ? "" : "s"}`;
    $("info").hidden = false;
    header = ["#", "T (\xB0C)", bmp ? "Presi\xF3n (hPa)" : "%RH", "epoch_min", local ? "Local" : "UTC"];
    rows = samples.map((s, i) => [
      String(i),
      s.tempC.toFixed(1),
      String(hVal(s.hRaw)),
      String(s.epochMin),
      local ? s.date.toLocaleString("sv-SE") : s.date.toISOString().replace("T", " ").replace(".000Z", "Z")
    ]);
    const tbl = $("tbl");
    tbl.tHead.innerHTML = "<tr>" + header.map((h) => `<th>${h}</th>`).join("") + "</tr>";
    tbl.tBodies[0].innerHTML = rows.map((r) => "<tr>" + r.map((c) => `<td>${c}</td>`).join("") + "</tr>").join("");
    if (showCharts && samples.length >= 2) {
      renderCharts($("charts"), [
        { title: "Temperatura (\xB0C)", pts: samples.map((s) => ({ x: s.epochMin, y: s.tempC })) },
        { title: bmp ? "Presi\xF3n (hPa)" : "Humedad (%RH)", pts: samples.map((s) => ({ x: s.epochMin, y: hVal(s.hRaw) })) }
      ], local ? -(/* @__PURE__ */ new Date()).getTimezoneOffset() : 0);
    }
    $("next").textContent = `tv ${samples[samples.length - 1].epochMin}`;
    $("panel").hidden = false;
    $("csv").disabled = false;
  }
  function select(i, focus = false) {
    active = i;
    render();
    if (focus) $("tabs").children[i].focus();
  }
  $("tabs").addEventListener("keydown", (e) => {
    const d = { ArrowRight: 1, ArrowLeft: -1 }[e.key];
    if (d) select((active + d + tabs.length) % tabs.length, true);
  });
  $("go").addEventListener("click", decode);
  $("toggle").addEventListener("click", () => {
    showCharts = !showCharts;
    $("toggle").textContent = `Gr\xE1ficas: ${showCharts ? "on" : "off"}`;
    $("toggle").setAttribute("aria-pressed", String(showCharts));
    savePrefs();
    render();
  });
  $("local").addEventListener("change", () => {
    savePrefs();
    render();
  });
  $("csv").addEventListener(
    "click",
    () => copy([header, ...rows].map((r) => r.join(",")).join("\n"), $("csv"))
  );
  $("copynext").addEventListener("click", () => copy($("next").textContent, $("copynext")));
  $("toggle").textContent = `Gr\xE1ficas: ${showCharts ? "on" : "off"}`;
  $("toggle").setAttribute("aria-pressed", String(showCharts));
  $("local").checked = loadPrefs().local ?? false;
})();
