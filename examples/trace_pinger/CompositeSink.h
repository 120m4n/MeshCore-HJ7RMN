#pragma once
#include "tracecore/Pinger.h"

// Difunde cada línea a dos transportes (Serial y BLE): los eventos salen por ambos (FR-004).
struct CompositeSink : tracecore::LineSink {
  tracecore::LineSink& a;
  tracecore::LineSink& b;
  CompositeSink(tracecore::LineSink& first, tracecore::LineSink& second) : a(first), b(second) {}
  void line(const char* s, size_t n) override { a.line(s, n); b.line(s, n); }
};
