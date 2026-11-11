#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "tracecore/Config.h"
#include "tracecore/Stats.h"
#include "tracecore/Version.h"

namespace tracecore {

// Recibe líneas completas SIN '\n'. El transporte añade el terminador de forma atómica.
struct LineSink {
  virtual ~LineSink() = default;
  virtual void line(const char* s, size_t n) = 0;
};

// Lo que el cascarón Arduino debe hacer tras un comando.
struct Action {
  enum Kind : uint8_t {
    None,
    Reconfigure,  // aplicar config() a la radio
    Measure       // leer `n` RSSI y devolver el resultado con emitMeas()
  };
  Kind kind = None;
  uint16_t n = 0;
};

// El catcher solo mide: no transmite nunca. Protocolo de líneas: HELLO, CFG?, SET freq=, MEAS [n].
class Catcher {
 public:
  Catcher(const RadioConfig& cfg, LineSink& out, uint32_t device_id);

  void emitBoot();  // #HELLO, #CFG
  Action onCommand(std::string_view line);
  void emitMeas(const RssiStats& r);

  const RadioConfig& config() const { return cfg_; }

 private:
  void say(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  RadioConfig cfg_;
  LineSink& out_;
  uint32_t id_;
  char buf_[160];
};

}  // namespace tracecore
