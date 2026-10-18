// Escáner I2C autónomo para XIAO nRF52840: no depende de MeshCore.
// Bus: SDA=D7, SCL=D6 (flags del env). Repite el escaneo cada 5 s.
#include <Arduino.h>
#include <Wire.h>

static uint8_t readReg(uint8_t addr, uint8_t reg, bool& ok) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  ok = Wire.endTransmission(false) == 0 && Wire.requestFrom(addr, (uint8_t)1) == 1;
  return ok ? Wire.read() : 0;
}

static const char* identify(uint8_t a) {
  bool ok;
  if (a == 0x76 || a == 0x77) {
    switch (readReg(a, 0xD0, ok)) {  // chip id
      case 0x60: return "BME280";
      case 0x56: case 0x57: case 0x58: return "BMP280";
      case 0x61: return "BME680/688";
      case 0x55: return "BMP085/180 (0x55?)";
      default: return ok ? "chip id desconocido en 0x76/77" : "sin respuesta a chip id";
    }
  }
  switch (a) {
    case 0x38: return "AHT10/20/21/25";
    case 0x40: return "SHT2x/HTU21/INA219/INA226 (0x40)";
    case 0x44: case 0x45: return "SHT3x/SHT4x";
    case 0x5A: return "CCS811/MLX90614/SGP (0x5A)";
    case 0x23: case 0x5C: return "BH1750";
    case 0x29: return "TSL2561/VL53L0X";
    case 0x48: case 0x49: return "ADS1115/TMP102/LM75";
    case 0x68: return "DS3231/MPU6050 (0x68)";
    case 0x20: case 0x27: return "PCF8574";
    default: return "?";
  }
}

static int scan() {
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[FOUND] addr=0x%02X -> %s\n", a, identify(a));
      n++;
    }
  }
  return n;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
  Wire.begin();
}

// ponytail: repite el escaneo cada 5 s para no perder el log si el monitor se abre tarde
void loop() {
  digitalToggle(LED_BUILTIN);
  if (Serial) {
    Serial.println("\n[SCAN] buscando 0x01..0x7E ...");
    int n = scan();
    Serial.printf("[SCAN] encontrados: %d\n", n);
    if (!n) Serial.println("[WARN] ningun ACK: cableado, 3V3/GND, pull-ups o sensor no I2C");
    Serial.println("[DONE] busqueda finalizada");
  }
  delay(5000);
}
