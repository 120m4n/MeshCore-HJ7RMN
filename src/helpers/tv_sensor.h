// src/helpers/tv_sensor.h
#pragma once
#include <stdint.h>

namespace tv {
bool sensor_ready();
int16_t read_temp_dC();
uint8_t read_hum_pct();
}
