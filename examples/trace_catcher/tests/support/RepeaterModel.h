#pragma once
// Modelo de la decisión TRACE de un repetidor MeshCore, SOLO para tests de host (spec 002 FR-002).
//
// Origen (Meshcore, fork 120m4n @ 362ea302):
//   src/Mesh.cpp                      Mesh::onRecvPacket, bloque TRACE (L42-67)  -> guardián W1
//   examples/simple_repeater/MyMesh.cpp  MyMesh::allowPacketForward (L442)      -> guardián W10
//   src/helpers/SimpleMeshTables.h    wasSeen/markSeen (tabla de vistos)
//   src/Packet.cpp                    calculatePacketHash                       -> guardián W6
// No se compila Mesh.cpp real porque arrastra Dispatcher, Identity/ed25519 y las tablas (research R-1);
// el guardián (tools/check_upstream_trace.py) falla si cualquiera de esos fragmentos cambia.
#include <Packet.h>

#include <array>
#include <set>

namespace tracecore_test {

enum class RepeaterAction { Release, Retransmit, TraceComplete };

struct RepeaterModel {
  uint8_t pub_key[32] = {0};
  bool disable_fwd = false;
  std::set<std::array<uint8_t, MAX_HASH_SIZE>> seen;  // wasSeen / markSeen

  // `snr` en dB tal como lo mediría la radio; muta `pkt` igual que upstream.
  RepeaterAction onRecv(mesh::Packet& pkt, float snr);
};

}  // namespace tracecore_test
