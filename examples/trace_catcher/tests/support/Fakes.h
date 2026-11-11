#pragma once
#include <memory>
#include <string>
#include <vector>

#include "tracecore/Catcher.h"

namespace fakes {

struct CapturingSink : tracecore::LineSink {
  std::vector<std::string> lines;
  void line(const char* s, size_t n) override { lines.emplace_back(s, n); }
};

struct Rig {
  CapturingSink out;
  std::unique_ptr<tracecore::Catcher> p;
  Rig() { p = std::make_unique<tracecore::Catcher>(tracecore::RadioConfig(), out, 0xC3F01A); }
};

}  // namespace fakes
