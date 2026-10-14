#pragma once
// BLE NUS (Nordic UART Service) del pinger: segundo transporte del protocolo de líneas `proto=1`
// (specs/003-ble-nus-yagiaim-link/contracts/ble-nus-gatt.md).
//
// Los callbacks de Bluefruit corren en otra tarea FreeRTOS y `tracecore::Pinger` no es seguro entre
// hilos (FR-019): la lectura y la escritura se hacen por sondeo desde loop(), y el callback de
// desconexión solo levanta una bandera.
#include <Arduino.h>
#include <bluefruit.h>

#include "tracecore/LineAssembler.h"
#include "tracecore/Pinger.h"

class BleTransport : public tracecore::LineSink {
 public:
  // Activa el SoftDevice, el servicio NUS y el advertising `YagiPinger-<4 hex>`.
  void begin();

  // Hay un central conectado y suscrito a las notificaciones.
  bool ready();

  // Una notificación por línea (BLEUart fragmenta según el MTU). No hace nada si no hay central.
  void line(const char* s, size_t n) override;

  // Lee lo recibido: on_line(std::string_view) por cada línea completa, on_overflow() si se descartó una.
  template <class OnLine, class OnOverflow>
  void poll(OnLine&& on_line, OnOverflow&& on_overflow) {
    while (bleuart_.available()) {
      switch (assembler_.push((uint8_t)bleuart_.read())) {
        case tracecore::LineAssembler::Result::Line: on_line(std::string_view(assembler_.line(), assembler_.length())); break;
        case tracecore::LineAssembler::Result::Overflow: on_overflow(); break;
        default: break;
      }
    }
  }

  // true una vez por cada desconexión del central (la bandera la levanta el callback de Bluefruit).
  bool takeDisconnected();

 private:
  BLEUart bleuart_;
  tracecore::LineAssembler assembler_;
};
