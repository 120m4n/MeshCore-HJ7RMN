#pragma once

namespace tracecore {

// Versión del catcher. La placa no tiene pantalla: la app la lee del #HELLO y es la única fuente de verdad.
// Subirla con cada cambio de firmware que la app tenga que distinguir.
constexpr const char* kFwVersion = "0.6.0";   // 0.6.0: catcher mínimo, solo MEAS (sin TRACE, RF, PONG ni LAB)
constexpr int kProto = 2;

}  // namespace tracecore
