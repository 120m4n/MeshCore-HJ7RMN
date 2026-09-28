// src/helpers/tv_telemetry.h — ring de 24 h + encoder de vectores "T,H,t;dT,dH,dt;..."
//
// Desviaciones respecto al doc de spec original (pedidas explícitamente):
//  - sample_tick() NO recibe/chequea clock_ok: se muestrea con el reloj interno
//    de la XIAO esté o no sincronizado.
//  - sample_tick() solo escribe si tv::sensor_ready() (ver tv_sensor.h) devuelve
//    true; esa función cachea el resultado de detectar BME280/BMP280 una sola
//    vez al boot y nunca reintenta si no se encontró nada.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace tv {

constexpr uint32_t INTERVAL_MIN = 15;
constexpr uint32_t SLOTS = 1440 / INTERVAL_MIN;  // 96

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

// Llamar desde loop(). No muestrea si no hay sensor soportado detectado.
inline void sample_tick(uint32_t now_min) {
  static uint32_t last_bucket = 0;
  if (!sensor_ready()) return;
  uint32_t b = now_min / INTERVAL_MIN;
  if (b == last_bucket) return;
  last_bucket = b;
  put(now_min, read_temp_dC(), read_hum_pct());
}

// Escribe en `out` (terminado en NUL) los registros con epoch_min > since,
// del más viejo al más nuevo, sin partir ningún registro. `cap` incluye el NUL
// y debe ser >= 32. Sin datos nuevos escribe "-". Devuelve el largo escrito.
inline size_t encode(uint32_t now_min, uint32_t since, char* out, size_t cap) {
  if (cap < 32) return 0;
  size_t n = 0;
  bool first = true;
  int16_t t0 = 0;
  uint8_t h0 = 0;
  uint32_t e0 = 0;
  const uint32_t start = (now_min / INTERVAL_MIN + 1) % SLOTS;  // slot más viejo

  for (uint32_t i = 0; i < SLOTS; i++) {
    const Rec& r = ring[(start + i) % SLOTS];
    if (!valid(r, now_min) || r.epoch_min <= since) continue;

    char buf[32];
    int len;
    if (first) {
      len = snprintf(buf, sizeof buf, "%d,%u,%lu",
                     (int)r.t, (unsigned)r.h, (unsigned long)r.epoch_min);
    } else {
      len = snprintf(buf, sizeof buf, ";%d,%d,%lu",
                     (int)r.t - (int)t0, (int)r.h - (int)h0,
                     (unsigned long)(r.epoch_min - e0));
    }
    if (len <= 0 || n + (size_t)len >= cap) break;  // no cabe: el resto va en la próxima consulta

    memcpy(out + n, buf, (size_t)len);
    n += (size_t)len;
    if (first) { t0 = r.t; h0 = r.h; e0 = r.epoch_min; first = false; }
  }

  if (first) { out[0] = '-'; out[1] = '\0'; return 1; }
  out[n] = '\0';
  return n;
}

// Gancho del comando remoto "tv <since>"; `args` apunta a lo que sigue a "tv ".
inline void handle_tv(const char* args, uint32_t now_min, char* reply, size_t cap) {
  uint32_t since = (uint32_t)strtoul(args, nullptr, 10);  // vacío o 0 = todo
  encode(now_min, since, reply, cap);
}

}  // namespace tv
