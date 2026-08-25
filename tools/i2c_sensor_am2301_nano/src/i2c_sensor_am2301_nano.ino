/*
 * i2c_sensor_am2301_nano
 *
 * Real I2C bridge for an AM2301 (DHT21) temperature/humidity sensor,
 * running on an Arduino Nano. The AM2301 has no native I2C interface -
 * it uses a bit-banged single-wire protocol with strict timing - so this
 * Nano reads it locally and re-exposes the last known-good reading as a
 * plain I2C slave the XIAO nRF52840 companion firmware can query with
 * the "TEMP_STATUS" channel command (see README_I2C_SENSOR.md at the
 * repo root).
 *
 * Wiring (Arduino Nano hardware I2C pins):
 *   SDA  -> A4  (to XIAO D7 / PIN_WIRE_SDA)
 *   SCL  -> A5  (to XIAO D6 / PIN_WIRE_SCL)
 *   GND  -> GND (common ground with the XIAO)
 *   4.7k pull-ups on SDA/SCL to 3.3V/5V if your wiring doesn't already
 *   have them (shared with the i2c_actuator_nano rig if both are on the
 *   same bus - do not duplicate pull-ups per device).
 *
 *   AM2301 DATA -> D2, with a 10k pull-up to VCC if your module doesn't
 *   already have one on its board (most 3/4-pin modules do).
 *
 * Protocol: read-only from the XIAO's perspective. A 5-byte I2C read
 * returns:
 *   byte 0:   status (0 = OK, 1 = cached, 2 = no reading yet)
 *   byte 1-2: temperature * 10, int16_t, big-endian (allows negative)
 *   byte 3-4: humidity * 10, uint16_t, big-endian
 * See AM2301Payload.h in the main MeshCore src tree for the decoder this
 * mirrors.
 *
 * The AM2301 is read in loop(), never inside onI2CRequest(): the sensor's
 * bit-banged protocol needs several ms with interrupts disabled, which is
 * incompatible with running inside an I2C ISR. loop() polls at most once
 * every READ_INTERVAL_MS (>=2000ms, the sensor's own minimum), caches the
 * last good reading, and onI2CRequest() only ever serves that cache.
 *
 * On-demand debug query (testing only): type 's' + Enter in the serial
 * monitor to print the currently cached reading immediately, without
 * waiting for the next periodic read or needing the XIAO/I2C master
 * connected at all - useful for bench-verifying the AM2301 wiring before
 * involving the rest of the mesh.
 */

#include <Wire.h>
#include <DHT.h>

#define I2C_SLAVE_ADDR    0x21   // must match AM2301_SENSOR_I2C_ADDR in variants/xiao_nrf52/platformio.ini
#define DHT_PIN           2      // AM2301 DATA pin -> Nano D2
#define DHT_TYPE          DHT21  // AM2301 = DHT21
#define READ_INTERVAL_MS  5000UL // AM2301 needs >=2000ms between reads

DHT dht(DHT_PIN, DHT_TYPE);

#define STATUS_OK             0
#define STATUS_CACHED         1
#define STATUS_NO_READING_YET 2

volatile uint8_t status_byte = STATUS_NO_READING_YET;
volatile int16_t cached_temp_x10 = 0;    // temperature * 10 (int16_t for atomic ISR access)
volatile uint16_t cached_hum_x10 = 0;    // humidity * 10 (uint16_t for atomic ISR access)
unsigned long last_read_ms = 0;

void readSensor() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    if (status_byte != STATUS_NO_READING_YET) {
      status_byte = STATUS_CACHED;
    }
    Serial.println("AM2301 read failed");
    return;
  }

  // Precompute integer values and cache them atomically (under critical section)
  // to ensure ISR never reads torn values when converting to I2C payload.
  int16_t t_x10 = (int16_t)(t * 10.0f);
  uint16_t h_x10 = (uint16_t)(h * 10.0f);

  noInterrupts();
  cached_temp_x10 = t_x10;
  cached_hum_x10 = h_x10;
  status_byte = STATUS_OK;
  interrupts();

  Serial.print("AM2301 read OK: temp=");
  Serial.print(t, 1);
  Serial.print("C hum=");
  Serial.print(h, 1);
  Serial.println("%");
}

// Debug-only helper: prints whatever is currently cached (readSensor()'s
// last result), without triggering a new read. Callable on demand from
// the serial monitor ('s' + Enter, see loop()) so you can bench-check the
// AM2301 wiring/values without the XIAO or any I2C traffic involved.
void printCachedReading() {
  Serial.print("Cached: status=");
  switch (status_byte) {
    case STATUS_OK:     Serial.print("OK"); break;
    case STATUS_CACHED: Serial.print("CACHED"); break;
    default:            Serial.print("NO_READING_YET"); break;
  }
  Serial.print(" temp=");
  Serial.print(cached_temp_x10 / 10.0, 1);
  Serial.print("C hum=");
  Serial.print(cached_hum_x10 / 10.0, 1);
  Serial.println("%");
}

void onI2CRequest() {
  // Snapshot cached values to avoid reading torn values if readSensor() updates
  // them while we're executing (though ISR context means interrupts are already
  // disabled, this snapshot keeps the intent explicit and safe).
  noInterrupts();
  int16_t temp_x10 = cached_temp_x10;
  uint16_t hum_x10 = cached_hum_x10;
  interrupts();

  uint8_t buf[5];
  buf[0] = status_byte;
  buf[1] = (uint8_t)(temp_x10 >> 8);
  buf[2] = (uint8_t)(temp_x10 & 0xFF);
  buf[3] = (uint8_t)(hum_x10 >> 8);
  buf[4] = (uint8_t)(hum_x10 & 0xFF);

  Wire.write(buf, sizeof(buf));
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onRequest(onI2CRequest);

  Serial.println("i2c_sensor_am2301_nano ready, listening as AM2301 bridge at 0x21");
  Serial.println("Type 's' + Enter to print the currently cached reading (debug/testing only)");
}

void loop() {
  unsigned long now = millis();
  if (now - last_read_ms >= READ_INTERVAL_MS) {
    last_read_ms = now;
    readSensor();
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 's' || c == 'S') {
      printCachedReading();
    }
  }
}
