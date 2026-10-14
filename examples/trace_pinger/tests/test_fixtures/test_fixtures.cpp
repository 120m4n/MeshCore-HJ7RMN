// T005/T006: lector de fixtures, mini-parser JSON plano y el archivo synthetic.txt.
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

#include "Fixtures.h"
#include "Hex.h"

using namespace tracecore_test;
namespace fs = std::filesystem;

static fs::path writeTemp(const std::string& content) {
  const fs::path p = fs::temp_directory_path() / ("fixtures_test_" + std::to_string(getpid()) + ".txt");
  std::ofstream(p) << content;
  return p;
}

TEST(MiniJson, ParsesFlatObjects) {
  Json j; std::string err;
  ASSERT_TRUE(parseJson(R"({"kind":"echo","snr_up_q":-25,"snr_dn":-4.5,"ok":true,"off":false,"s":"a\"b"})", j, err));
  EXPECT_EQ(j.str("kind"), "echo");
  EXPECT_EQ(j.integer("snr_up_q"), -25);
  EXPECT_NEAR(j.num("snr_dn"), -4.5, 1e-9);
  EXPECT_NEAR(j.num("snr_up_q"), -25, 1e-9);  // un entero también sirve como número
  EXPECT_TRUE(j.boolean("ok"));
  EXPECT_FALSE(j.boolean("off", true));
  EXPECT_EQ(j.str("s"), "a\"b");
  EXPECT_FALSE(j.has("missing"));
  EXPECT_EQ(j.str("missing", "def"), "def");
}

TEST(MiniJson, AcceptsEmptyObject) {
  Json j; std::string err;
  EXPECT_TRUE(parseJson("{}", j, err));
  EXPECT_TRUE(j.f.empty());
}

TEST(MiniJson, RejectsInvalidAndNested) {
  Json j; std::string err;
  EXPECT_FALSE(parseJson("", j, err));
  EXPECT_FALSE(parseJson("[1]", j, err));
  EXPECT_FALSE(parseJson(R"({"a":1)", j, err));
  EXPECT_FALSE(parseJson(R"({"a":1} x)", j, err));
  EXPECT_FALSE(parseJson(R"({"a":{"b":1}})", j, err));
  EXPECT_FALSE(parseJson(R"({"a":[1,2]})", j, err));
  EXPECT_FALSE(parseJson(R"({"a":null})", j, err));
  EXPECT_FALSE(parseJson(R"({"a" 1})", j, err));
}

TEST(FixtureReader, SkipsCommentsAndBlankLinesAndKeepsPayloads) {
  const auto p = writeTemp("# comentario\n\n2601E7\t{\"kind\":\"echo\"}\r\n  \n0D00\t{\"kind\":\"other\"}\n");
  std::vector<Entry> v; std::vector<std::string> errs;
  EXPECT_TRUE(readFixtureFile(p.string(), v, errs));
  EXPECT_TRUE(errs.empty());
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0].payload, "2601E7");
  EXPECT_EQ(v[1].json.str("kind"), "other");
  EXPECT_NE(v[1].where.find(":5"), std::string::npos);  // "archivo:línea"
  fs::remove(p);
}

TEST(FixtureReader, LineWithoutExpectationFailsWithHowToAnnotate) {  // US3-AS3
  const auto p = writeTemp("2601E7123456780000000000A1\n");
  std::vector<Entry> v; std::vector<std::string> errs;
  EXPECT_FALSE(readFixtureFile(p.string(), v, errs));
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_NE(errs[0].find("sin expectativa"), std::string::npos);
  EXPECT_NE(errs[0].find("\"kind\""), std::string::npos);  // el mensaje enseña el formato
  EXPECT_TRUE(v.empty());
  fs::remove(p);
}

TEST(FixtureReader, TabWithoutJsonAndBadJsonFail) {
  const auto p = writeTemp("2601\t\n2602\t{oops}\n2603\t{\"kind\":\"other\"}\n");
  std::vector<Entry> v; std::vector<std::string> errs;
  EXPECT_FALSE(readFixtureFile(p.string(), v, errs));
  EXPECT_EQ(errs.size(), 2u);
  EXPECT_EQ(v.size(), 1u);  // las líneas buenas se siguen leyendo
  fs::remove(p);
}

TEST(FixtureReader, MissingFileIsAnError) {
  std::vector<Entry> v; std::vector<std::string> errs;
  EXPECT_FALSE(readFixtureFile("/nonexistent/frames.txt", v, errs));
  EXPECT_EQ(errs.size(), 1u);
}

TEST(SyntheticFixtures, FileParsesAndEveryEntryIsWellFormed) {  // T006
  const char* dir = std::getenv("PINGER_DIR");
  if (!dir) GTEST_SKIP() << "ejecutar con run_tests.sh (PINGER_DIR)";
  std::vector<Entry> v; std::vector<std::string> errs;
  ASSERT_TRUE(readFixtureFile(std::string(dir) + "/tests/fixtures/frames/synthetic.txt", v, errs));
  EXPECT_TRUE(errs.empty());
  EXPECT_GE(v.size(), 12u);

  const std::set<std::string> kinds = {"echo", "request", "other", "invalid"};
  std::set<std::string> seen;
  for (const auto& e : v) {
    std::vector<uint8_t> bytes;
    EXPECT_TRUE(fromHex(e.payload, bytes));
    EXPECT_TRUE(kinds.count(e.json.str("kind")) == 1);
    seen.insert(e.json.str("kind"));
    if (e.json.str("kind") == "echo") {
      EXPECT_TRUE(e.json.has("tag") && e.json.has("hash") && e.json.has("snr_up_q"));
    } else if (e.json.str("kind") == "request") {
      EXPECT_TRUE(e.json.has("tag") && e.json.has("hash"));
    } else if (e.json.str("kind") == "other") {
      EXPECT_TRUE(e.json.has("route") && e.json.has("type"));
    } else {
      EXPECT_TRUE(e.json.has("reason"));
    }
  }
  EXPECT_EQ(seen.size(), kinds.size());  // hay de los cuatro tipos
}
