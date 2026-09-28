// src/helpers/tv_sensor.cpp — detección + lectura entera para tv_telemetry.h
#ifdef WITH_TV_TELEMETRY

#include "tv_sensor.h"
#include <math.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>

namespace tv {
namespace {

enum class Kind : uint8_t { NONE, BME280, BMP280 };

bool probed = false;
Kind detected = Kind::NONE;
Adafruit_BME280 bme;
Adafruit_BMP280 bmp(&Wire);

void probe() {
  probed = true;
  if (bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire)) {
    detected = Kind::BME280;
  } else if (bmp.begin(0x76) || bmp.begin(0x77)) {
    detected = Kind::BMP280;
  }
  // si ninguno responde, detected queda en NONE para siempre: sensor_ready()
  // nunca vuelve a escanear el bus (requisito: si no hay sensor, no arranca nunca).
}

}  // namespace

bool sensor_ready() {
  if (!probed) probe();
  return detected != Kind::NONE;
}

int16_t read_temp_dC() {
  float c = (detected == Kind::BME280) ? bme.readTemperature() : bmp.readTemperature();
  return (int16_t)lroundf(c * 10.0f);
}

uint8_t read_hum_pct() {
  // ponytail: BMP280 no mide humedad; en ese caso se reporta 0 fijo.
  // Upgrade: si hace falta H real con solo BMP280 en el bus, hay que sumar
  // un sensor de humedad separado -- fuera del alcance de este spec.
  if (detected != Kind::BME280) return 0;
  float h = bme.readHumidity();
  if (h < 0) h = 0;
  if (h > 100) h = 100;
  return (uint8_t)lroundf(h);
}

const char* sensor_kind() {
  if (!probed) probe();
  switch (detected) {
    case Kind::BME280: return "BME280";
    case Kind::BMP280: return "BMP280";
    default: return "none";
  }
}

}  // namespace tv

#endif // WITH_TV_TELEMETRY
