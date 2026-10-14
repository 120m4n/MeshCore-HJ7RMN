// US4 / T022: paridad del codec de líneas. Los mismos fixtures que consumirá el parser Swift (fase 003)
// validan aquí el lado C++: JSON -> struct -> format*() == línea, y comando -> respuesta esperada.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "Fakes.h"
#include "Fixtures.h"
#include "Hex.h"
#include "tracecore/LineCodec.h"

using namespace tracecore;
using namespace tracecore_test;
using namespace fakes;

// Carga un archivo de tests/fixtures/lines; los errores de lectura (sin expectativa, JSON malo) fallan el test.
static bool load(const char* file, std::vector<Entry>& out) {
  const char* dir = std::getenv("PINGER_DIR");
  if (!dir) return false;
  std::vector<std::string> errors;
  readFixtureFile(std::string(dir) + "/tests/fixtures/lines/" + file, out, errors);
  for (const auto& m : errors) { std::printf("  %s\n", m.c_str()); EXPECT_TRUE(false); }
  return true;
}

// Compara `got` con la línea del fixture; si difiere, imprime dónde y qué se obtuvo.
static void expectLine(const Entry& e, const std::string& got) {
  const bool same = e.payload == got;
  if (!same) std::printf("  %s\n    esperado: %s\n    obtenido: %s\n", e.where.c_str(), e.payload.c_str(), got.c_str());
  EXPECT_TRUE(same);
}

static Status statusOf(const std::string& s) {
  return s == "ok" ? Status::Ok : s == "timeout" ? Status::Timeout : s == "busy" ? Status::Busy : Status::TxErr;
}
static int32_t scaled(const Json& j, const char* k, double scale) { return (int32_t)std::lround(j.num(k) * scale); }

TEST(LineFixtures, PingLinesRoundtrip) {
  std::vector<Entry> v;
  if (!load("events_p.txt", v)) GTEST_SKIP() << "ejecutar con run_tests.sh (PINGER_DIR)";
  ASSERT_GE(v.size(), 7u);
  for (const auto& e : v) {
    EXPECT_EQ(e.json.str("type"), "P");
    PingResult r;
    r.seq = (uint32_t)e.json.integer("seq");
    r.tag = (uint32_t)std::strtoul(e.json.str("tag").c_str(), nullptr, 16);
    r.status = statusOf(e.json.str("status"));
    r.rtt_ms = (uint32_t)e.json.integer("rtt_ms");
    r.snr_up_c = scaled(e.json, "snr_up", 100);
    r.snr_dn_c = scaled(e.json, "snr_dn", 100);
    r.rssi_dn_d = scaled(e.json, "rssi_dn", 10);
    r.n_resp = (uint8_t)e.json.integer("n_resp");
    char b[kMaxLine];
    formatPing(b, sizeof b, r);
    expectLine(e, b);
  }
}

TEST(LineFixtures, SummaryLinesRoundtripAndLossMatches) {
  std::vector<Entry> v;
  if (!load("events_s.txt", v)) GTEST_SKIP() << "ejecutar con run_tests.sh (PINGER_DIR)";
  ASSERT_GE(v.size(), 7u);
  for (const auto& e : v) {
    EXPECT_EQ(e.json.str("type"), "S");
    BurstSummary s;
    s.n = (uint16_t)e.json.integer("n");
    s.ok = (uint16_t)e.json.integer("ok");
    s.timeouts = (uint16_t)e.json.integer("timeouts");
    s.busy = (uint16_t)e.json.integer("busy");
    s.has_median = e.json.has("med_snr_up");
    s.med_up_c = scaled(e.json, "med_snr_up", 100);
    s.med_dn_c = scaled(e.json, "med_snr_dn", 100);
    s.med_rssi_d = scaled(e.json, "med_rssi_dn", 10);
    char b[kMaxLine];
    formatSummary(b, sizeof b, s);
    expectLine(e, b);

    // loss_pct del JSON == tercer campo de la línea (vacío si el JSON no lo trae)
    std::string field = e.payload;
    for (int i = 0; i < 3; i++) field = field.substr(field.find(',') + 1);
    field = field.substr(0, field.find(','));
    EXPECT_EQ(field, e.json.has("loss_pct") ? std::to_string(e.json.integer("loss_pct")) : std::string());
  }
}

TEST(LineFixtures, RawLinesRoundtripIncludingTruncation) {
  std::vector<Entry> v;
  if (!load("events_r.txt", v)) GTEST_SKIP() << "ejecutar con run_tests.sh (PINGER_DIR)";
  ASSERT_GE(v.size(), 5u);
  for (const auto& e : v) {
    EXPECT_EQ(e.json.str("type"), "R");
    std::string hex = e.json.str("hex");
    if (hex.size() > 2 && hex.compare(hex.size() - 2, 2, "..") == 0) hex.resize(hex.size() - 2);
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(fromHex(hex, bytes));
    if (e.json.has("total_len")) bytes.resize((size_t)e.json.integer("total_len"), 0xAB);  // la línea muestra solo 64 B
    char b[kMaxLine];
    formatRaw(b, sizeof b, (uint32_t)e.json.integer("t_ms"), scaled(e.json, "rssi", 10), scaled(e.json, "snr", 100),
              bytes.data(), bytes.size());
    expectLine(e, b);
    EXPECT_LE(e.payload.size(), kMaxLine - 1);
  }
}

TEST(LineFixtures, CommandsProduceTheExpectedReplies) {
  std::vector<Entry> v;
  if (!load("commands.txt", v)) GTEST_SKIP() << "ejecutar con run_tests.sh (PINGER_DIR)";
  ASSERT_GE(v.size(), 40u);
  for (const auto& e : v) {
    const std::string setup = e.json.str("setup");
    Rig r(setup != "nohash");  // por defecto hay hash A1
    if (setup == "mon") r.p->onCommand("MON on", 0);
    if (setup == "pending") r.p->onCommand("PING", 0);
    r.out.lines.clear();
    r.p->onCommand(e.payload, 10);

    const std::string got = r.out.lines.empty() ? "(sin respuesta)" : r.out.lines[0];
    bool ok = got == e.json.str("reply");
    if (e.json.has("reply2")) ok = ok && r.out.lines.size() > 1 && r.out.lines[1] == e.json.str("reply2");
    if (!ok) std::printf("  %s\n    comando:  %s\n    esperado: %s\n    obtenido: %s\n", e.where.c_str(), e.payload.c_str(),
                         e.json.str("reply").c_str(), got.c_str());
    EXPECT_TRUE(ok);

    // `error` del JSON == código de la respuesta "!ERR <code> ..."
    if (e.json.has("error")) EXPECT_EQ(got.rfind("!ERR " + e.json.str("error"), 0), 0u);
  }
}

TEST(LineFixtures, EveryErrorCodeTheFirmwareEmitsIsCovered) {
  std::vector<Entry> v;
  if (!load("commands.txt", v)) GTEST_SKIP() << "ejecutar con run_tests.sh (PINGER_DIR)";
  std::string all;
  for (const auto& e : v) all += e.json.str("reply") + "\n";
  for (const char* want : {"!ERR syntax", "!ERR unknown", "!ERR range", "!ERR state nohash", "!ERR state mon", "!ERR state busy"})
    EXPECT_NE(all.find(want), std::string::npos);
}

TEST(LineFixtures, InfoLinesAreWellFormed) {
  std::vector<Entry> v;
  if (!load("info.txt", v)) GTEST_SKIP() << "ejecutar con run_tests.sh (PINGER_DIR)";
  ASSERT_GE(v.size(), 20u);
  for (const auto& e : v) {
    const std::string t = e.json.str("type");
    const std::string prefix = t == "ERR" ? "!ERR " : "#" + t + " ";
    const bool ok = e.payload.rfind(prefix, 0) == 0 && e.payload.size() < kMaxLine;
    if (!ok) std::printf("  %s: forma inesperada: %s\n", e.where.c_str(), e.payload.c_str());
    EXPECT_TRUE(ok);
  }
}
