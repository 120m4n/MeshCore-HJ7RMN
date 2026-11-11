#pragma once

namespace tracecore {

// Versión de los dos firmwares del laboratorio (catcher y ponger). La placa no tiene pantalla: la app la lee del
// #HELLO y es la única fuente de verdad. Subirla con cada cambio de firmware que la app tenga que distinguir.
constexpr const char* kFwVersion = "0.8.0";   // 0.8.0: sin PONG/RF/LAST/MON/SAVE/wait; BLE MeshAz-; arranca en catcher
constexpr int kProto = 3;

}  // namespace tracecore
