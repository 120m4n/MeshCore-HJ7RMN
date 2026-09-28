// src/helpers/tv_sensor.h
#pragma once
#include <stdint.h>

namespace tv {
bool sensor_ready();
int16_t read_temp_dC();
// Con BME280: %RH real (0-100). Con BMP280 (no mide humedad): presión
// atmosférica escalada (hPa - 800, saturada a [0,255]) -- ver tv_sensor.cpp.
// Usar sensor_kind() para saber cuál de los dos es antes de interpretar el valor.
uint8_t read_hum_pct();
const char* sensor_kind();  // "BME280", "BMP280" o "none" -- diagnóstico, comando "tv sensor"
}
