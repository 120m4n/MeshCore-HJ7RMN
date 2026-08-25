#ifdef HAS_AM2301_SENSOR

#include "AM2301RemoteSensor.h"
#include <MeshCore.h>

void AM2301RemoteSensor::begin(TwoWire& wire, uint8_t i2c_addr) {
  _wire = &wire;
  _addr = i2c_addr;
}

bool AM2301RemoteSensor::read(float* temp_c, float* hum_pct, uint8_t* status) {
  if (_wire == NULL || _wire->requestFrom((uint8_t)_addr, (uint8_t)5) != 5) {
    MESH_DEBUG_PRINTLN("AM2301RemoteSensor: I2C read failed");
    return false;
  }

  uint8_t buf[5];
  for (uint8_t i = 0; i < 5; i++) {
    buf[i] = _wire->read();
  }

  decodeAM2301Payload(buf, temp_c, hum_pct, status);
  return true;
}

#endif // ifdef HAS_AM2301_SENSOR
