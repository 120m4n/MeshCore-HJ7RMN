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
    // Mismo modo que EnvironmentSensorManager (forzado, X1): el chip es
    // compartido y su takeForcedMeasurement() lo deja dormido; en modo normal
    // esta instancia leería el último valor congelado para siempre.
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_OFF);
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

// sample_tick() llama read_temp_dC() y enseguida read_hum_pct(): la medición
// forzada de acá sirve para las dos lecturas.
int16_t read_temp_dC() {
  if (detected == Kind::BME280) bme.takeForcedMeasurement();
  float c = (detected == Kind::BME280) ? bme.readTemperature() : bmp.readTemperature();
  return (int16_t)lroundf(c * 10.0f);
}

uint8_t read_hum_pct() {
  if (detected == Kind::BME280) {
    float h = bme.readHumidity();
    if (h < 0) h = 0;
    if (h > 100) h = 100;
    return (uint8_t)lroundf(h);
  }
  // ponytail: BMP280 no mide humedad. En vez de mandar un "0%RH" inventado
  // (que se lee como un dato real y no lo es), este campo pasa a llevar
  // presión atmosférica escalada: (hPa - 800), saturado a [0,255] ->
  // cubre 800-1055 hPa (nivel del mar normal + variación de clima).
  // El header del vector tv lleva sensor_kind_id() para que el consumidor
  // sepa si este campo es %RH o presión.
  // Upgrade: si hace falta %RH real con un BMP280 en el bus, hay que sumar
  // un sensor de humedad separado -- fuera del alcance de este spec.
  float hpa = bmp.readPressure() / 100.0f;  // Pa -> hPa
  float scaled = hpa - 800.0f;
  if (scaled < 0) scaled = 0;
  if (scaled > 255) scaled = 255;
  return (uint8_t)lroundf(scaled);
}

uint8_t sensor_kind_id() {
  if (!probed) probe();
  return (uint8_t)detected;  // Kind: NONE=0, BME280=1, BMP280=2
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
