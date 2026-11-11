#include <gtest/gtest.h>

#include <vector>

#include <Utils.h>

#include "tracecore/LabFrame.h"

using namespace tracecore;

// Cuerpos de oro SIN los 4 bytes de auth: el host compila con el SHA256 de mentira de
// test/mocks/SHA256.h, así que la auth real no se puede comprobar aquí. Los vectores
// completos (auth = sha256(labkey ‖ cuerpo)[0..4), calculados con hashlib) están en
// docs/specs/005-lab-protocol/contracts/lab-frame.md para las apps.
// ponytail: auth real sin verificar en host; añadir un SHA-256 real de test si las apps divergen.
static const char* kKey = "password";
static const uint32_t kHash = 0xA1B2C3D4;
static const uint32_t kTag = 0x11223344;
static const std::vector<uint8_t> kGet = {0x4C, 0x02, 0xA1, 0xB2, 0xC3, 0xD4, 0x11, 0x22, 0x33, 0x44, 0x07, 0x01};
static const std::vector<uint8_t> kSetTx = {0x4C, 0x02, 0xA1, 0xB2, 0xC3, 0xD4, 0x11, 0x22, 0x33, 0x44, 0x08, 0x02, 0x02};
static const std::vector<uint8_t> kSetRadio = {0x4C, 0x02, 0xA1, 0xB2, 0xC3, 0xD4, 0x11, 0x22, 0x33, 0x44, 0x09, 0x03,
                                               0x58, 0xF9, 0x0D, 0x00, 0x08, 0x0A, 0x05};
static const std::vector<uint8_t> kResp = {0x4C, 0x82, 0xA1, 0xB2, 0xC3, 0xD4, 0x11, 0x22, 0x33, 0x44, 0x09, 0x00,
                                           0x58, 0xF9, 0x0D, 0x00, 0x08, 0x0A, 0x05, 0x02, 0x16, 0xC4, 0x28};

static std::vector<uint8_t> build(const LabCommand& c, const char* key = kKey) {
  uint8_t b[kLabMaxLen];
  return std::vector<uint8_t>(b, b + buildCommand(c, key, b, sizeof b));
}

static std::vector<uint8_t> body(const std::vector<uint8_t>& v) {
  return std::vector<uint8_t>(v.begin(), v.end() - kLabAuthLen);
}

static LabCommand cmd(uint8_t seq, LabOp op) {
  LabCommand c; c.dst = kHash; c.tag = kTag; c.seq = seq; c.op = (uint8_t)op;
  return c;
}

static LabResponse resp() {
  LabResponse r;
  r.src = kHash; r.tag = kTag; r.seq = 9; r.status = LabStatus::Ok; r.radio = LabRadio{915800, 8, 10, 5};
  r.tx_dbm = 2; r.tx_ceiling = 22; r.rssi_cmd = -60; r.snr_cmd_q = 40;
  return r;
}

TEST(LabFrame, BuildCommandsMatchGolden) {
  EXPECT_EQ(body(build(cmd(7, LabOp::Get))), kGet);
  auto t = cmd(8, LabOp::SetTx); t.tx_dbm = 2;
  EXPECT_EQ(body(build(t)), kSetTx);
  auto r = cmd(9, LabOp::SetRadio); r.radio = LabRadio{915800, 8, 10, 5};
  EXPECT_EQ(body(build(r)), kSetRadio);
  EXPECT_EQ(build(cmd(1, LabOp::Reset)).size(), 16u);
}

// La causa del wasSeen: tras reiniciar el catcher, seq se repite. Con otro tag, la trama es otra (otra huella).
TEST(LabFrame, SameSeqDifferentTagIsADifferentFrame) {
  auto a = cmd(1, LabOp::Get), b = a;
  b.tag = 0x55667788;
  EXPECT_NE(build(a), build(b));
  const auto f = build(b);
  auto c = parseCommand(f.data(), f.size(), kHash, kKey);
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->tag, 0x55667788u);
}

TEST(LabFrame, ParseCommandRoundTrip) {
  auto r = cmd(9, LabOp::SetRadio); r.radio = LabRadio{915800, 8, 10, 5};
  auto f = build(r);
  auto c = parseCommand(f.data(), f.size(), kHash, kKey);
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->seq, 9);
  EXPECT_EQ(c->op, (uint8_t)LabOp::SetRadio);
  EXPECT_EQ(c->radio.freq_khz, 915800u);
  EXPECT_EQ(c->radio.bw_code, 8);
  EXPECT_EQ(c->radio.sf, 10);
  EXPECT_EQ(c->radio.cr, 5);
  auto t = cmd(8, LabOp::SetTx); t.tx_dbm = -9;
  f = build(t);
  auto p = parseCommand(f.data(), f.size(), kHash, kKey);
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->tx_dbm, -9);
}

TEST(LabFrame, ParseCommandRejects) {
  auto t = cmd(8, LabOp::SetTx); t.tx_dbm = 2;
  const auto v = build(t);
  EXPECT_FALSE(parseCommand(v.data(), v.size(), 0xA1B2C3D5, kKey).has_value());  // otro ponger
  EXPECT_FALSE(parseCommand(v.data(), v.size(), kHash, "otra").has_value());      // labkey errónea
  EXPECT_FALSE(parseCommand(v.data(), v.size() - 1, kHash, kKey).has_value());    // longitud
  // (alterar un arg no se puede probar aquí: el SHA256 de mentira colisiona; con el real sí falla)
  auto bad = v; bad.back() ^= 1; EXPECT_FALSE(parseCommand(bad.data(), bad.size(), kHash, kKey).has_value());  // auth alterada
  bad = v;      bad[0] = 0x4D;  EXPECT_FALSE(parseCommand(bad.data(), bad.size(), kHash, kKey).has_value());
  bad = v;      bad[1] = 0x01;  EXPECT_FALSE(parseCommand(bad.data(), bad.size(), kHash, kKey).has_value());
  uint8_t rb[kLabMaxLen];
  const size_t rn = buildResponse(resp(), kKey, rb, sizeof rb);
  EXPECT_FALSE(parseCommand(rb, rn, kHash, kKey).has_value());  // una respuesta no es comando
}

TEST(LabFrame, UnknownOpWithValidAuthIsReturned) {
  EXPECT_TRUE(build(cmd(1, (LabOp)0x7F)).empty());  // no se construyen ops desconocidas
  // Pero si llega una con auth válida, se devuelve para que el ponger conteste BadOp.
  uint8_t f[16] = {0x4C, 0x02, 0xA1, 0xB2, 0xC3, 0xD4, 0x11, 0x22, 0x33, 0x44, 0x0A, 0x7F};
  mesh::Utils::sha256(f + 12, 4, (const uint8_t*)kKey, 8, f, 12);  // mismo sellado que el codec
  auto c = parseCommand(f, sizeof f, kHash, kKey);
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->op, 0x7F);
  EXPECT_EQ(c->seq, 0x0A);
  f[15] ^= 1;
  EXPECT_FALSE(parseCommand(f, sizeof f, kHash, kKey).has_value());
}

TEST(LabFrame, ResponseGoldenAndRoundTrip) {
  uint8_t b[kLabMaxLen];
  ASSERT_EQ(buildResponse(resp(), kKey, b, sizeof b), kLabRespLen);
  EXPECT_EQ(std::vector<uint8_t>(b, b + kLabRespLen - kLabAuthLen), kResp);

  auto p = parseResponse(b, kLabRespLen, kHash, kKey);
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->seq, 9);
  EXPECT_EQ(p->tag, kTag);
  EXPECT_EQ(p->status, LabStatus::Ok);
  EXPECT_EQ(p->radio.freq_khz, 915800u);
  EXPECT_EQ(p->tx_dbm, 2);
  EXPECT_EQ(p->tx_ceiling, 22);
  EXPECT_EQ(p->rssi_cmd, -60);
  EXPECT_EQ(p->snr_cmd_q, 40);
  EXPECT_FALSE(parseResponse(b, kLabRespLen, 0x01020304, kKey).has_value());
  EXPECT_FALSE(parseResponse(b, kLabRespLen, kHash, "otra").has_value());
  const auto g = build(cmd(7, LabOp::Get));
  EXPECT_FALSE(parseResponse(g.data(), g.size(), kHash, kKey).has_value());
}

TEST(LabFrame, BwCodesAndRadioRange) {
  EXPECT_EQ(labBwCode(250), 8);
  EXPECT_EQ(labBwCode(62.5f), 6);
  EXPECT_EQ(labBwCode(7.8f), 0);
  EXPECT_EQ(labBwCode(41.7f), 5);
  EXPECT_EQ(labBwCode(200), -1);
  EXPECT_TRUE(labRadioValid(LabRadio{915800, 8, 10, 5}));
  EXPECT_FALSE(labRadioValid(LabRadio{915800, 10, 10, 5}));
  EXPECT_FALSE(labRadioValid(LabRadio{915800, 8, 13, 5}));
  EXPECT_FALSE(labRadioValid(LabRadio{915800, 8, 10, 9}));
  EXPECT_FALSE(labRadioValid(LabRadio{50000, 8, 10, 5}));
}

#include "../support/test_main.h"
