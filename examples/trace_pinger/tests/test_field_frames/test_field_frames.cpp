// US3 / T018: reproduce los fixtures de tramas (sintéticos y capturas de campo) contra el
// parser upstream real y contra tracecore. Reglas por `kind` en contracts/fixture-format.md.
#include <Packet.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "Fixtures.h"
#include "Hex.h"
#include "tracecore/TraceFrame.h"

using namespace tracecore_test;
namespace fs = std::filesystem;

static uint32_t hex32(const std::string& s) { return (uint32_t)std::strtoul(s.c_str(), nullptr, 16); }

// true si la trama cumple su expectativa; si no, `why` explica qué falló.
static bool verifyEntry(const Entry& e, std::string& why) {
  std::vector<uint8_t> b;
  if (!fromHex(e.payload, b) || b.empty()) { why = "hex inválido o vacío"; return false; }
  if (b.size() > 255) { why = "trama de más de 255 bytes"; return false; }

  const std::string kind = e.json.str("kind");
  const uint8_t hash = (uint8_t)hex32(e.json.str("hash", "A1"));
  const auto echo = tracecore::parseEcho(b.data(), b.size(), hash);
  mesh::Packet p;
  const bool upstream = p.readFrom(b.data(), (uint8_t)b.size());

  if (kind == "echo") {
    if (!e.json.has("tag") || !e.json.has("hash") || !e.json.has("snr_up_q")) { why = "echo requiere tag, hash y snr_up_q"; return false; }
    if (!echo) { why = "parseEcho rechazó un eco"; return false; }
    if (echo->snr_up_q != e.json.integer("snr_up_q")) { why = "snr_up_q distinto: parseEcho=" + std::to_string(echo->snr_up_q); return false; }
    if (echo->tag != hex32(e.json.str("tag"))) { why = "tag distinto"; return false; }
    if (!upstream) { why = "readFrom upstream rechazó un eco"; return false; }
    return true;
  }
  if (kind == "request") {
    if (!e.json.has("tag") || !e.json.has("hash")) { why = "request requiere tag y hash"; return false; }
    uint8_t want[tracecore::kRequestLen];
    tracecore::buildRequest(hex32(e.json.str("tag")), hash, want);
    if (b.size() != sizeof want || std::memcmp(want, b.data(), sizeof want) != 0) { why = "buildRequest(tag,hash) no coincide con los bytes"; return false; }
    if (!upstream) { why = "readFrom upstream rechazó la petición"; return false; }
    return true;
  }
  if (kind == "other") {
    if (!e.json.has("route") || !e.json.has("type")) { why = "other requiere route y type"; return false; }
    if (echo) { why = "parseEcho aceptó una trama que no es eco"; return false; }
    if (!upstream) { why = "readFrom upstream rechazó la trama"; return false; }
    if (p.getRouteType() != e.json.integer("route") || p.getPayloadType() != e.json.integer("type")) {
      why = "route/type distintos: route=" + std::to_string(p.getRouteType()) + " type=" + std::to_string(p.getPayloadType());
      return false;
    }
    return true;
  }
  if (kind == "invalid") {
    if (!e.json.has("reason")) { why = "invalid requiere reason"; return false; }
    if (echo) { why = "parseEcho aceptó una trama inválida"; return false; }
    if (e.json.has("readFrom") && upstream != e.json.boolean("readFrom")) {
      why = std::string("readFrom upstream devolvió ") + (upstream ? "true" : "false");
      return false;
    }
    return true;
  }
  why = "kind desconocido: '" + kind + "' (usa echo, request, other o invalid)";
  return false;
}

TEST(FieldFrames, EveryFixtureFileMatchesTheRealCode) {
  const char* dir = std::getenv("PINGER_DIR");
  if (!dir) GTEST_SKIP() << "ejecutar con run_tests.sh (PINGER_DIR)";
  const fs::path frames = fs::path(dir) / "tests/fixtures/frames";

  std::vector<std::string> files;
  for (const auto& f : fs::directory_iterator(frames))
    if (f.path().extension() == ".txt") files.push_back(f.path().string());
  std::sort(files.begin(), files.end());
  ASSERT_TRUE(!files.empty());

  size_t total = 0;
  for (const auto& path : files) {
    std::vector<Entry> entries;
    std::vector<std::string> errors;
    readFixtureFile(path, entries, errors);
    for (const auto& m : errors) { std::printf("  %s\n", m.c_str()); EXPECT_TRUE(false); }  // sin expectativa / JSON inválido
    for (const auto& e : entries) {
      std::string why;
      const bool ok = verifyEntry(e, why);
      if (!ok) std::printf("  %s: %s\n", e.where.c_str(), why.c_str());
      EXPECT_TRUE(ok);
      total++;
    }
  }
  std::printf("  (%zu fixtures en %zu archivos)\n", total, files.size());
  EXPECT_GE(total, 13u);
}

// El verificador tiene que fallar cuando la expectativa está mal: si no, los fixtures no protegen nada.
static Entry entry(const std::string& hex, const std::string& json) {
  Entry e; std::string err;
  e.payload = hex; e.where = "memoria";
  parseJson(json, e.json, err);
  return e;
}

TEST(FieldFrames, VerifierRejectsWrongExpectations) {
  const std::string echo = "2601E7123456780000000000A1";
  std::string why;
  EXPECT_TRUE(verifyEntry(entry(echo, R"({"kind":"echo","tag":"78563412","hash":"A1","snr_up_q":-25})"), why));
  EXPECT_FALSE(verifyEntry(entry(echo, R"({"kind":"echo","tag":"78563412","hash":"A1","snr_up_q":-24})"), why));
  EXPECT_FALSE(verifyEntry(entry(echo, R"({"kind":"echo","tag":"78563413","hash":"A1","snr_up_q":-25})"), why));
  EXPECT_FALSE(verifyEntry(entry(echo, R"({"kind":"echo","tag":"78563412","hash":"B2","snr_up_q":-25})"), why));
  EXPECT_FALSE(verifyEntry(entry(echo, R"({"kind":"invalid","reason":"x","hash":"A1"})"), why));  // sí es eco válido
  EXPECT_FALSE(verifyEntry(entry(echo, R"({"kind":"other","route":2,"type":9})"), why));          // parseEcho lo acepta
  EXPECT_FALSE(verifyEntry(entry(echo, R"({"kind":"mistery"})"), why));
  EXPECT_NE(why.find("kind desconocido"), std::string::npos);
  EXPECT_FALSE(verifyEntry(entry("26C1E7123456780000000000A1", R"({"kind":"invalid","reason":"x","readFrom":true})"), why));
  EXPECT_FALSE(verifyEntry(entry("ZZ", R"({"kind":"other","route":1,"type":3})"), why));
  EXPECT_FALSE(verifyEntry(entry("2600123456780000000000A1", R"({"kind":"request","tag":"78563499","hash":"A1"})"), why));
}
