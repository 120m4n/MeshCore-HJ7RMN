#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

// Protocolo LAB catcher -> ponger (docs/specs/005-lab-protocol/contracts/lab-frame.md).
// El codec trabaja sobre el PAYLOAD de un paquete RAW_CUSTOM DIRECT. En el aire la trama
// es {kLabHeader, 0x00 (path_len), payload...}: el catcher antepone/quita esos 2 bytes y
// el ponger usa createRawData() + sendDirect() con path vacío.
//
// Los hashes son los 4 primeros bytes de la pubkey leídos como hex: "A1B2C3D4" = 0xA1B2C3D4
// y se escriben en orden big-endian (A1 primero).

namespace tracecore {

constexpr uint8_t kLabHeader = 0x3E;   // v0 | RAW_CUSTOM (0x0F) | DIRECT
constexpr uint8_t kLabMagic = 0x4C;    // 'L'
constexpr uint8_t kLabVersion = 0x02;   // v2: tag aleatorio de 4 bytes (cada trama única, como el TRACE)
constexpr uint8_t kLabRespBit = 0x80;
constexpr size_t kLabAuthLen = 4;
constexpr size_t kLabRespLen = 27;
constexpr size_t kLabMaxLen = 32;      // cabe cualquier comando o respuesta

enum class LabOp : uint8_t { Get = 0x01, SetTx = 0x02, SetRadio = 0x03, Reset = 0x04 };
enum class LabStatus : uint8_t { Ok = 0, Range = 1, BadOp = 2 };

struct LabRadio {
  uint32_t freq_khz = 0;
  uint8_t bw_code = 0;   // índice en kLabBwHz
  uint8_t sf = 0;
  uint8_t cr = 0;
};

struct LabCommand {
  uint32_t dst = 0;
  uint32_t tag = 0;      // aleatorio por trama: wasSeen de MeshCore no confunde un comando nuevo con uno visto
  uint8_t seq = 0;
  uint8_t op = 0;        // LabOp; crudo para que el ponger pueda responder BadOp
  int8_t tx_dbm = 0;     // SetTx
  LabRadio radio;        // SetRadio
};

struct LabResponse {
  uint32_t src = 0;
  uint32_t tag = 0;      // eco del tag del comando
  uint8_t seq = 0;
  LabStatus status = LabStatus::Ok;
  LabRadio radio;
  int8_t tx_dbm = 0;
  int8_t tx_ceiling = 0;
  int8_t rssi_cmd = 0;   // dBm con que el ponger oyó el comando (= rssi_up real)
  int8_t snr_cmd_q = 0;  // SNR del comando en cuartos de dB
};

extern const uint32_t kLabBwHz[10];

// -1 si khz no es un ancho de banda LoRa conocido (tolerancia 0,1 kHz).
int labBwCode(float khz);
// bw_code < 10, sf 5..12, cr 5..8, freq 100..1100 MHz. El techo de TX lo valida el ponger.
bool labRadioValid(const LabRadio& r);

// Devuelven la longitud escrita, o 0 si no cabe / op desconocida al construir.
size_t buildCommand(const LabCommand& c, const char* key, uint8_t* out, size_t cap);
size_t buildResponse(const LabResponse& r, const char* key, uint8_t* out, size_t cap);

// nullopt si: magic/versión/longitud/hash no coinciden o la auth falla.
// Un op desconocido con auth válida SÍ se devuelve (el ponger contesta BadOp).
std::optional<LabCommand> parseCommand(const uint8_t* b, size_t n, uint32_t my_hash, const char* key);
std::optional<LabResponse> parseResponse(const uint8_t* b, size_t n, uint32_t src_hash, const char* key);

}  // namespace tracecore
