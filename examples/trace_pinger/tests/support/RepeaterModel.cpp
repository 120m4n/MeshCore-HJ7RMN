#include "RepeaterModel.h"

#include <cstring>

namespace tracecore_test {

RepeaterAction RepeaterModel::onRecv(mesh::Packet& pkt, float snr) {
  pkt._snr = (int8_t)(snr * 4);  // la radio entrega el SNR en cuartos de dB, truncado hacia cero

  if (!(pkt.isRouteDirect() && pkt.getPayloadType() == PAYLOAD_TYPE_TRACE)) return RepeaterAction::Release;
  if (pkt.path_len >= MAX_PATH_SIZE) return RepeaterAction::Release;

  uint8_t i = 4 + 4;  // tras trace_tag(4) y auth_code(4)
  const uint8_t flags = pkt.payload[i++];
  const uint8_t path_sz = flags & 0x03;  // v1.11+: tamaño del hash de ruta

  const uint8_t len = pkt.payload_len - i;  // igual que upstream (uint8_t)
  const uint16_t offset = (uint16_t)pkt.path_len << path_sz;
  if (offset >= len) return RepeaterAction::TraceComplete;  // fin de ruta: onTraceRecv()

  const bool match = std::memcmp(&pkt.payload[i + offset], pub_key, 1 << path_sz) == 0;
  const bool allow = !disable_fwd;  // allowPacketForward para DIRECT: solo disable_fwd aplica
  if (!match || !allow) return RepeaterAction::Release;

  std::array<uint8_t, MAX_HASH_SIZE> h;
  pkt.calculatePacketHash(h.data());
  if (seen.count(h)) return RepeaterAction::Release;  // wasSeen
  seen.insert(h);                                      // markSeen

  pkt.path[pkt.path_len++] = (int8_t)(pkt.getSNR() * 4);  // SNR, no hash
  return RepeaterAction::Retransmit;
}

}  // namespace tracecore_test
