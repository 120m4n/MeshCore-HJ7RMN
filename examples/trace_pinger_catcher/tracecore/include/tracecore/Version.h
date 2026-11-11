#pragma once

namespace tracecore {

// Versión de los dos firmwares del laboratorio (catcher y ponger). La placa no tiene pantalla: la app la lee del
// #HELLO y es la única fuente de verdad. Subirla con cada cambio de firmware que la app tenga que distinguir.
constexpr const char* kFwVersion = "0.7.0";   // 0.7.0: una sola tarjeta, MODE catcher|pinger (arranca en catcher)
constexpr int kProto = 3;

}  // namespace tracecore
