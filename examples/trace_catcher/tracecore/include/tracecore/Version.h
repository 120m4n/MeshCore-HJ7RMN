#pragma once

namespace tracecore {

// Versión de los dos firmwares del laboratorio (catcher y ponger). La placa no tiene pantalla: la app la lee del
// #HELLO y es la única fuente de verdad. Subirla con cada cambio de firmware que la app tenga que distinguir.
constexpr const char* kFwVersion = "0.5.2";
constexpr int kProto = 2;

}  // namespace tracecore
