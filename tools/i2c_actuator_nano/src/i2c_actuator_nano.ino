/*
 * i2c_actuator_nano
 *
 * Test double for a PCF8574 I2C GPIO expander, running on an Arduino
 * Nano. Lets you bench-test the MeshCore "actuator on public channel"
 * feature (see README_I2C.md at the repo root) end-to-end without
 * needing a real PCF8574 chip or actuator: the Nano acts as the I2C
 * slave the XIAO nRF52840 companion firmware talks to, and mirrors
 * the 8-bit output state on 8 digital pins you can watch with a
 * multimeter, LEDs, or a scope.
 *
 * Wiring (Arduino Nano hardware I2C pins):
 *   SDA -> A4  (to XIAO D7 / PIN_WIRE_SDA)
 *   SCL -> A5  (to XIAO D6 / PIN_WIRE_SCL)
 *   GND -> GND (common ground with the XIAO)
 *   4.7k pull-ups on SDA/SCL to 3.3V/5V if your wiring doesn't already
 *   have them (a real PCF8574 module usually includes them).
 *
 * Output pins D2..D9 mirror PCF8574 bits 0..7. The MeshCore firmware
 * can drive any of the 8 independently via the "PIN<n>_ON" / "PIN<n>_OFF"
 * command (n = 0-7), so watch Dx = D2 + n for a given pin n.
 *
 * Protocol emulated: a single-byte I2C write sets all 8 quasi-
 * bidirectional pins at once, same as a real PCF8574 - no internal
 * registers, no read-back logic needed for this test rig.
 */

#include <Wire.h>

#define I2C_SLAVE_ADDR   0x20   // must match PCF8574_I2C_ADDR in platformio.ini
#define NUM_PINS         8
const uint8_t OUTPUT_PINS[NUM_PINS] = { 2, 3, 4, 5, 6, 7, 8, 9 };

volatile uint8_t last_state = 0xFF;   // PCF8574 pins idle HIGH

void applyState(uint8_t state) {
  for (uint8_t i = 0; i < NUM_PINS; i++) {
    digitalWrite(OUTPUT_PINS[i], (state & (1 << i)) ? HIGH : LOW);
  }
}

void onI2CReceive(int num_bytes) {
  if (num_bytes < 1) return;
  uint8_t state = 0xFF;
  while (Wire.available()) {
    state = Wire.read();   // PCF8574 only cares about the last byte written
  }

  uint8_t prev_state = last_state;
  last_state = state;
  applyState(state);

  Serial.print("I2C write: 0x");
  Serial.println(state, HEX);

  // with multiple independently-addressable pins, call out exactly which
  // bit(s) flipped so a multi-pin command is easy to verify at a glance.
  uint8_t changed = prev_state ^ state;
  for (uint8_t i = 0; i < NUM_PINS; i++) {
    if (changed & (1 << i)) {
      Serial.print("  pin ");
      Serial.print(i);
      Serial.print(" -> ");
      Serial.println((state & (1 << i)) ? "HIGH" : "LOW");
    }
  }
}

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < NUM_PINS; i++) {
    pinMode(OUTPUT_PINS[i], OUTPUT);
  }
  applyState(last_state);

  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(onI2CReceive);

  Serial.println("i2c_actuator_nano ready, listening as PCF8574 stand-in at 0x20");
}

void loop() {
  // all the work happens in onI2CReceive(); nothing to poll
}
