// src/helpers/tv_telemetry.h — ring de 24 h + encoder de vectores compactos (formato v1)
//
// Desviaciones respecto al doc de spec original (pedidas explícitamente):
//  - sample_tick() NO recibe/chequea clock_ok: se muestrea con el reloj interno
//    de la XIAO esté o no sincronizado.
//  - sample_tick() solo escribe si tv::sensor_ready() (ver tv_sensor.h) devuelve
//    true; esa función cachea el resultado de detectar BME280/BMP280 una sola
//    vez al boot y nunca reintenta si no se encontró nada.
//  - El campo `H` del vector es %RH solo con BME280. Con BMP280 (que no mide
//    humedad) lleva presión atmosférica escalada en su lugar -- ver el
//    comentario de read_hum_pct() en tv_sensor.cpp. El header del vector
//    trae cuál de los dos aplica.
//  - Formato v1 compacto en base64url (ver encode()), no el "T,H,t;..." del spec.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace tv {

constexpr uint32_t INTERVAL_MIN = 30;
constexpr uint32_t SLOTS = 1440 / INTERVAL_MIN;  // 48

struct Rec {
  uint32_t epoch_min;  // 0 = slot vacío
  int16_t  t;          // décimas de °C
  uint8_t  h;          // %RH
};

static Rec ring[SLOTS];  // en BSS: arranca en cero

inline void put(uint32_t now_min, int16_t t, uint8_t h) {
  ring[(now_min / INTERVAL_MIN) % SLOTS] = { now_min, t, h };
}

inline bool valid(const Rec& r, uint32_t now_min) {
  return r.epoch_min != 0 && r.epoch_min <= now_min && (now_min - r.epoch_min) < 1440;
}

// Ganchos del sensor: implementados en tv_sensor.cpp (ver Task 2).
// sensor_ready() se cachea la primera vez que se llama y nunca vuelve a
// escanear el bus si no encontró nada soportado.
bool sensor_ready();
int16_t read_temp_dC();
uint8_t read_hum_pct();
const char* sensor_kind();  // "BME280" / "BMP280" / "none" -- diagnóstico, comando "tv sensor"
uint8_t sensor_kind_id();   // 0 none, 1 BME280, 2 BMP280 -- va en el header del vector

// Llamar desde loop(). No muestrea si no hay sensor soportado detectado.
inline void sample_tick(uint32_t now_min) {
  static uint32_t last_bucket = 0;
  if (!sensor_ready()) return;
  uint32_t b = now_min / INTERVAL_MIN;
  if (b == last_bucket) return;
  last_bucket = b;
  put(now_min, read_temp_dC(), read_hum_pct());
}

// Formato v1 (base64url, 6 bits por char, sin separadores):
//   V NN EEEEE TT HH  |  dT dH  |  .k  |  ~TTHH
//   V     = (versión 1 << 2) | sensor_kind_id()  -> 'F' BME280, 'G' BMP280
//   NN    = cantidad de registros (detecta truncamiento)
//   EEEEE = epoch_min exacto del anchor; TT = zigzag(T décimas); HH = H
//   dT dH = 1 char zigzag c/u (-32..31), delta vs registro ANTERIOR, slot siguiente
//   .k    = saltar k slots vacíos (1..63; se repite si el hueco es mayor)
//   ~TTHH = registro con T/H absolutos (el delta no entraba en 1 char)
// El tiempo de cada registro después del anchor es implícito: inicio de su
// bucket de 30 min. Por eso `since` se compara por bucket, no por minuto: el
// backend puede mandar el epoch decodificado del último registro tal cual.
constexpr char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
constexpr uint8_t FORMAT_VERSION = 1;

inline uint32_t zigzag(int32_t v) { return v >= 0 ? (uint32_t)v << 1 : ((uint32_t)(-v) << 1) - 1; }

inline size_t put64(char* p, uint32_t v, int nchars) {
  for (int i = 0; i < nchars; i++) p[i] = B64[(v >> (6 * (nchars - 1 - i))) & 63];
  return (size_t)nchars;
}

// Escribe en `out` (terminado en NUL) los registros con bucket > bucket(since),
// del más viejo al más nuevo, sin partir ningún registro. `cap` incluye el NUL
// y debe ser >= 32. Sin datos nuevos escribe "-". Devuelve el largo escrito.
inline size_t encode(uint32_t now_min, uint32_t since, char* out, size_t cap) {
  if (cap < 32) return 0;
  const uint32_t since_b = since / INTERVAL_MIN;
  size_t n = 0;
  uint32_t count = 0, pb = 0;
  int16_t pt = 0;
  uint8_t ph = 0;
  const uint32_t start = (now_min / INTERVAL_MIN + 1) % SLOTS;  // slot más viejo

  for (uint32_t i = 0; i < SLOTS; i++) {
    const Rec& r = ring[(start + i) % SLOTS];
    const uint32_t b = r.epoch_min / INTERVAL_MIN;
    if (!valid(r, now_min) || b <= since_b || (count && b <= pb)) continue;

    char buf[16];
    size_t len = 0;
    if (count == 0) {
      buf[len++] = B64[(FORMAT_VERSION << 2) | sensor_kind_id()];
      len += 2;  // NN: se completa al final
      len += put64(buf + len, r.epoch_min, 5);
      len += put64(buf + len, zigzag(r.t), 2);
      len += put64(buf + len, r.h, 2);
    } else {
      for (uint32_t gap = b - pb - 1; gap;) {
        uint32_t k = gap > 63 ? 63 : gap;
        buf[len++] = '.';
        buf[len++] = B64[k];
        gap -= k;
      }
      const int dt = r.t - pt, dh = r.h - ph;
      if (dt < -32 || dt > 31 || dh < -32 || dh > 31) {
        buf[len++] = '~';
        len += put64(buf + len, zigzag(r.t), 2);
        len += put64(buf + len, r.h, 2);
      } else {
        buf[len++] = B64[zigzag(dt)];
        buf[len++] = B64[zigzag(dh)];
      }
    }
    if (n + len >= cap) break;  // no cabe: el resto va en la próxima consulta

    memcpy(out + n, buf, len);
    n += len;
    count++;
    pb = b; pt = r.t; ph = r.h;
  }

  if (!count) { out[0] = '-'; out[1] = '\0'; return 1; }
  put64(out + 1, count, 2);
  out[n] = '\0';
  return n;
}

// Gancho del comando remoto "tv <since>"; `args` apunta a lo que sigue a "tv ".
inline void handle_tv(const char* args, uint32_t now_min, char* reply, size_t cap) {
  uint32_t since = (uint32_t)strtoul(args, nullptr, 10);  // vacío o 0 = todo
  encode(now_min, since, reply, cap);
}

}  // namespace tv
