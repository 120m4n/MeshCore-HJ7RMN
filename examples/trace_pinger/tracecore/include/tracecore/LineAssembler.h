#pragma once
#include <cstddef>
#include <cstdint>

#include "tracecore/LineCodec.h"

namespace tracecore {

// Reensambla líneas terminadas en '\n' a partir de bytes sueltos (Serial) o de escrituras BLE en
// trozos arbitrarios. Mismas reglas que el LineAssembler de la app: tolera '\r\n', ignora líneas
// vacías y, si una línea pasa de kMaxLine-1 bytes de contenido, la descarta entera y se resincroniza
// en el siguiente '\n' (el llamador responde `!ERR syntax`).
class LineAssembler {
 public:
  enum class Result : uint8_t { None, Line, Overflow };

  // Alimenta un byte. Con Result::Line, `line()`/`length()` son válidos hasta el siguiente push().
  Result push(uint8_t c) {
    if (ready_) { len_ = 0; ready_ = false; }
    if (c == '\n') {
      if (discarding_) { discarding_ = false; len_ = 0; return Result::Overflow; }
      if (len_ > 0 && buf_[len_ - 1] == '\r') len_--;
      if (len_ == 0) return Result::None;
      buf_[len_] = '\0';
      ready_ = true;
      return Result::Line;
    }
    if (discarding_) return Result::None;
    if (len_ >= kMaxLine - 1) { discarding_ = true; len_ = 0; return Result::None; }
    buf_[len_++] = (char)c;
    return Result::None;
  }

  const char* line() const { return buf_; }
  size_t length() const { return len_; }
  void reset() { len_ = 0; ready_ = false; discarding_ = false; }

 private:
  char buf_[kMaxLine] = {0};
  size_t len_ = 0;
  bool ready_ = false, discarding_ = false;
};

}  // namespace tracecore
