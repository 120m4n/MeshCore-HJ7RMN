// US4 / T022: paridad del codec de líneas. Los mismos fixtures que consumirá el parser Swift (fase 003)
// validan aquí el lado C++: JSON -> struct -> format*() == línea, y comando -> respuesta esperada.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "Fakes.h"
#include "Fixtures.h"
#include "tracecore/LineCodec.h"

using namespace tracecore;
using namespace tracecore_test;
using namespace fakes;

// Carga un archivo de tests/fixtures/lines; los errores de lectura (sin expectativa, JSON malo) fallan el test.
static bool load(const char* file, std::vector<Entry>& out) {
  const char* dir = std::getenv("CATCHER_DIR");
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

TEST(LineFixtures, MeasLinesRoundtrip) {
  std::vector<Entry> v;
  if (!load("events_m.txt", v)) GTEST_SKIP() << "ejecutar con run_tests.sh (CATCHER_DIR)";
  ASSERT_GE(v.size(), 6u);
  for (const auto& e : v) {
    EXPECT_EQ(e.json.str("type"), "M");
    RssiStats r;
    r.n = (uint16_t)e.json.integer("n");
    r.kept = (uint16_t)e.json.integer("kept");
    r.mean = (float)e.json.num("mean");
    r.sd = (float)e.json.num("sd");
    r.median = (float)e.json.num("median");
    r.min = (float)e.json.num("min");
    r.max = (float)e.json.num("max");
    char b[kMaxLine];
    formatMeas(b, sizeof b, r);
    expectLine(e, b);
  }
}

TEST(LineFixtures, CommandsProduceTheExpectedReplies) {
  std::vector<Entry> v;
  if (!load("commands.txt", v)) GTEST_SKIP() << "ejecutar con run_tests.sh (CATCHER_DIR)";
  ASSERT_GE(v.size(), 10u);
  for (const auto& e : v) {
    Rig r;
    r.p->onCommand(e.payload);
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
  if (!load("commands.txt", v)) GTEST_SKIP() << "ejecutar con run_tests.sh (CATCHER_DIR)";
  std::string all;
  for (const auto& e : v) all += e.json.str("reply") + "\n";
  for (const char* want : {"!ERR syntax", "!ERR unknown", "!ERR range"})
    EXPECT_NE(all.find(want), std::string::npos);
}

TEST(LineFixtures, InfoLinesAreWellFormed) {
  std::vector<Entry> v;
  if (!load("info.txt", v)) GTEST_SKIP() << "ejecutar con run_tests.sh (CATCHER_DIR)";
  ASSERT_GE(v.size(), 8u);
  for (const auto& e : v) {
    const std::string t = e.json.str("type");
    const std::string prefix = t == "ERR" ? "!ERR " : "#" + t + " ";
    const bool ok = e.payload.rfind(prefix, 0) == 0 && e.payload.size() < kMaxLine;
    if (!ok) std::printf("  %s: forma inesperada: %s\n", e.where.c_str(), e.payload.c_str());
    EXPECT_TRUE(ok);
  }
}
