// US2-AS6 / SC-002: el guardián detecta cambios dentro de los fragmentos vigilados, y solo esos.
// Corre tools/check_upstream_trace.py sobre el core real y sobre copias mutadas (research R-5).
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static const char* kWatched[] = {"src/Mesh.cpp", "src/Packet.h", "src/Packet.cpp", "src/MeshCore.h",
                                 "examples/simple_repeater/MyMesh.cpp"};

static std::string slurp(const fs::path& p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
static void spit(const fs::path& p, const std::string& s) { std::ofstream(p) << s; }

static bool replaceOnce(const fs::path& p, const std::string& from, const std::string& to) {
  std::string s = slurp(p);
  const size_t i = s.find(from);
  if (i == std::string::npos) return false;
  s.replace(i, from.size(), to);
  spit(p, s);
  return true;
}

struct Guard {
  std::string root, dir;
  fs::path tmp;
  bool ok = false;

  Guard() {
    const char* r = std::getenv("MESHCORE_ROOT");
    const char* d = std::getenv("PINGER_DIR");
    if (!r || !d) return;
    root = r; dir = d;
    ok = true;
  }
  // Copia de los archivos vigilados en un directorio temporal.
  fs::path copyCore() {
    tmp = fs::temp_directory_path() / ("guard_test_" + std::to_string(getpid()));
    fs::remove_all(tmp);
    for (const char* rel : kWatched) {
      fs::create_directories((tmp / rel).parent_path());
      fs::copy_file(fs::path(root) / rel, tmp / rel);
    }
    return tmp;
  }
  int run(const std::string& rootArg) const {
    const std::string cmd = "python3 -I " + dir + "/tools/check_upstream_trace.py --root " + rootArg +
                            " --lock " + dir + "/tests/upstream-trace.lock > /dev/null 2>&1";
    const int st = std::system(cmd.c_str());
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
  }
  ~Guard() { if (!tmp.empty()) fs::remove_all(tmp); }
};

static bool havePython() { return std::system("python3 --version > /dev/null 2>&1") == 0; }

TEST(UpstreamGuard, PassesOnTheRealCore) {  // (a)
  Guard g;
  if (!g.ok) GTEST_SKIP() << "ejecutar con run_tests.sh (MESHCORE_ROOT/PINGER_DIR)";
  if (!havePython()) GTEST_SKIP() << "python3 no disponible";
  EXPECT_EQ(g.run(g.root), 0);
}

TEST(UpstreamGuard, DetectsMutationInsideW1) {  // (b) SC-002
  Guard g;
  if (!g.ok) GTEST_SKIP() << "ejecutar con run_tests.sh";
  if (!havePython()) GTEST_SKIP() << "python3 no disponible";
  const fs::path t = g.copyCore();
  ASSERT_TRUE(replaceOnce(t / "src/Mesh.cpp", "pkt->getSNR()*4", "pkt->getSNR()*5"));
  EXPECT_EQ(g.run(t.string()), 1);
}

TEST(UpstreamGuard, DetectsMutationInsideOtherFragments) {
  Guard g;
  if (!g.ok) GTEST_SKIP() << "ejecutar con run_tests.sh";
  if (!havePython()) GTEST_SKIP() << "python3 no disponible";
  const fs::path t = g.copyCore();
  ASSERT_TRUE(replaceOnce(t / "src/MeshCore.h", "#define MAX_HASH_SIZE        8", "#define MAX_HASH_SIZE        9"));
  EXPECT_EQ(g.run(t.string()), 1);  // W8
}

TEST(UpstreamGuard, IgnoresChangesOutsideTheFragments) {  // (c)
  Guard g;
  if (!g.ok) GTEST_SKIP() << "ejecutar con run_tests.sh";
  if (!havePython()) GTEST_SKIP() << "python3 no disponible";
  const fs::path t = g.copyCore();
  spit(t / "src/Mesh.cpp", slurp(t / "src/Mesh.cpp") + "\nint unrelated_marker = 1;\n");
  EXPECT_EQ(g.run(t.string()), 0);
}

TEST(UpstreamGuard, IgnoresCommentsAndWhitespaceInsideFragments) {
  Guard g;
  if (!g.ok) GTEST_SKIP() << "ejecutar con run_tests.sh";
  if (!havePython()) GTEST_SKIP() << "python3 no disponible";
  const fs::path t = g.copyCore();
  ASSERT_TRUE(replaceOnce(t / "src/Mesh.cpp", "// append SNR (Not hash!)", "// otro comentario   \n\n"));
  EXPECT_EQ(g.run(t.string()), 0);
}

TEST(UpstreamGuard, MissingAnchorExitsWithTwo) {  // (d)
  Guard g;
  if (!g.ok) GTEST_SKIP() << "ejecutar con run_tests.sh";
  if (!havePython()) GTEST_SKIP() << "python3 no disponible";
  const fs::path t = g.copyCore();
  ASSERT_TRUE(replaceOnce(t / "src/Mesh.cpp", "Packet* Mesh::createTrace(", "Packet* Mesh::createTraceRenamed("));
  EXPECT_EQ(g.run(t.string()), 2);
}
