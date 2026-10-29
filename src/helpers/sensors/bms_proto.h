// Protocol reverse-engineered from "SMART BMS.apk" (com.inuker.bluetooth.data.*).
// Modbus-RTU over BLE: slave 0xD2, FC 0x03 read / 0x06 write / 0x10 write-multi,
// CRC-16/MODBUS appended low byte first. Plain C so it builds on ESP32 and on a PC.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define BMS_ADDR 0xD2
#define BMS_RUN_REGS 62  // getReadDataByLocNumber(0, 62) -> reply byte count 0x7C

static inline uint16_t bms_crc16(const uint8_t *p, size_t n) {
  uint16_t c = 0xFFFF;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; i++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
  }
  return c;
}

// CreateCtrDataHelper.getReadDataByLocNumber: D2 03 <start> <count> <crc>
static inline void bms_build_read(uint8_t out[8], uint16_t start, uint16_t count) {
  uint8_t f[8] = {BMS_ADDR, 0x03, (uint8_t)(start >> 8), (uint8_t)start, (uint8_t)(count >> 8), (uint8_t)count};
  uint16_t c = bms_crc16(f, 6);
  f[6] = c & 0xFF; f[7] = c >> 8;
  memcpy(out, f, 8);
}

typedef struct {
  uint16_t cell_mv[32];    // reg 0-31
  int16_t temp_c[8];       // reg 32-39, raw-40
  float pack_v;            // reg 40 *0.1
  float current_a;         // reg 41 (raw-30000)*0.1, + = charge
  float soc;               // reg 42 *0.1 %
  uint16_t max_cell_mv, min_cell_mv;  // reg 43, 44
  int16_t max_temp_c, min_temp_c;     // reg 45, 46
  uint8_t state;           // reg 47: 0 idle, 1 charging, 2 discharging
  float remain_ah;         // reg 48 *0.1
  uint16_t cells, temps;   // reg 49, 50
  uint16_t cycles;         // reg 51
  uint8_t balancing, chg_mos, dsg_mos;  // reg 52, 53, 54
  uint16_t avg_cell_mv, diff_cell_mv;   // reg 55, 56
  uint16_t power_w;        // reg 57 (app shows /1000 kW)
  uint16_t alarm[4];       // reg 58-61 bitfields
} bms_run_t;

static inline void bms_decode_run(const uint8_t *d, bms_run_t *r) {
  uint16_t g[BMS_RUN_REGS];
  for (int i = 0; i < BMS_RUN_REGS; i++) g[i] = (d[2 * i] << 8) | d[2 * i + 1];
  for (int i = 0; i < 32; i++) r->cell_mv[i] = g[i];
  for (int i = 0; i < 8; i++) r->temp_c[i] = (int16_t)g[32 + i] - 40;
  r->pack_v = g[40] * 0.1f;
  r->current_a = ((int32_t)g[41] - 30000) * 0.1f;
  r->soc = g[42] * 0.1f;
  r->max_cell_mv = g[43]; r->min_cell_mv = g[44];
  r->max_temp_c = (int16_t)g[45] - 40; r->min_temp_c = (int16_t)g[46] - 40;
  r->state = g[47];
  r->remain_ah = g[48] * 0.1f;
  r->cells = g[49]; r->temps = g[50]; r->cycles = g[51];
  r->balancing = g[52] != 0; r->chg_mos = g[53] != 0; r->dsg_mos = g[54] != 0;
  r->avg_cell_mv = g[55]; r->diff_cell_mv = g[56];
  r->power_w = g[57];
  for (int i = 0; i < 4; i++) r->alarm[i] = g[58 + i];
}

// Notifications arrive in MTU-sized chunks; reassemble like BluetoothRegulate.SyncData.
typedef struct { uint8_t buf[512]; size_t len; } bms_rx_t;

static inline void bms_drop(bms_rx_t *rx, size_t n) {
  memmove(rx->buf, rx->buf + n, rx->len - n);
  rx->len -= n;
}

// Returns 1 when a full, CRC-valid run-data frame was decoded into *out.
static inline int bms_feed(bms_rx_t *rx, const uint8_t *p, size_t n, bms_run_t *out) {
  if (n > sizeof rx->buf - rx->len) rx->len = 0;  // overflow: resync
  if (n > sizeof rx->buf) return 0;
  memcpy(rx->buf + rx->len, p, n);
  rx->len += n;
  int got = 0;
  while (rx->len >= 5) {
    uint8_t *b = rx->buf;
    size_t need;
    if (b[0] == BMS_ADDR && b[1] == 0x03) need = b[2] + 5;               // read reply
    else if (b[0] == BMS_ADDR && (b[1] == 0x06 || b[1] == 0x10)) need = 8; // write ack
    else { bms_drop(rx, 1); continue; }
    if (rx->len < need) break;
    if (bms_crc16(b, need - 2) != (b[need - 2] | (b[need - 1] << 8))) { bms_drop(rx, 1); continue; }
    if (b[1] == 0x03 && b[2] == BMS_RUN_REGS * 2) { bms_decode_run(b + 3, out); got = 1; }
    bms_drop(rx, need);
  }
  return got;
}
