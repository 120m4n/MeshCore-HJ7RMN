/*
 * i2c_actuator_esp8266
 *
 * Test double for a PCF8574 I2C GPIO expander, running on a real
 * ESP8266-12E NodeMCU dev board. Lets you bench-test the MeshCore
 * "actuator on public channel" feature (see README_I2C.md at the repo
 * root) end-to-end without needing a real PCF8574 chip or actuator:
 * the NodeMCU acts as the I2C slave the XIAO nRF52840 companion
 * firmware talks to, and mirrors the 8-bit output state on digital
 * pins you can watch with a multimeter, LEDs, or a scope.
 *
 * This is an independent derivative of ../i2c_actuator_nano (the same
 * test double running on an Arduino Nano). Both sketches are kept as
 * separate, self-contained PlatformIO projects - neither depends on
 * the other, and neither needs to be touched to build/flash the other.
 *
 * Wiring (NodeMCU I2C pins, ESP8266 Wire default mapping):
 *   SDA -> D2 / GPIO4  (to XIAO D7 / PIN_WIRE_SDA)
 *   SCL -> D1 / GPIO5  (to XIAO D6 / PIN_WIRE_SCL)
 *   GND -> GND (common ground with the XIAO)
 *   4.7k pull-ups on SDA/SCL to 3.3V if your wiring doesn't already
 *   have them (a real PCF8574 module usually includes them). Use
 *   3.3V, NOT 5V - the ESP8266 is not 5V-tolerant.
 *
 * Output pins: unlike the Nano (8 free digital pins), a NodeMCU only
 * has 7 GPIOs left over once D1/D2 are spent on I2C and D9/D10 stay
 * reserved for the onboard USB-serial UART (hardwired, can't be
 * repurposed). So only bits 0-6 of the emulated PCF8574 get a
 * physical pin; bit 7 is tracked in software only (still visible in
 * the Serial log and in the raw byte returned on an I2C read, so
 * PIN_STATUS / readState() drift-detection on the companion side is
 * unaffected - it just has no wire to probe with a multimeter for
 * that one bit). See this folder's README.md for the full pin table.
 *
 *   Dx  GPIO  PCF8574 bit
 *   D0  16    0
 *   D3  0     1
 *   D4  2     2
 *   D5  14    3
 *   D6  12    4
 *   D7  13    5
 *   D8  15    6
 *   --  --    7   (software-only, no pin)
 *
 * D3/D4/D8 are ESP8266 boot-strapping pins (GPIO0/GPIO2/GPIO15). They
 * only matter during reset/power-up; once running they behave as
 * plain GPIO like any other pin, same as on any NodeMCU relay-shield
 * project. D4 is also wired to the onboard blue LED (active LOW) on
 * most NodeMCU boards, so bit 2 will visibly blink that LED too.
 *
 * Protocol emulated: a single-byte I2C write sets all 8 quasi-
 * bidirectional pins at once, same as a real PCF8574. A read returns
 * that same byte (see onI2CRequest below), so the companion firmware's
 * readState() can be tested against this rig too.
 *
 * Simulating a power loss on the chip: type 'r' + Enter in the serial
 * monitor to force last_state back to 0xFF (power-on default) without
 * going through onI2CReceive - this mimics the PCF8574 losing power on
 * its own supply rail while the XIAO companion keeps running with a
 * now-stale cached state, so you can test readState()'s drift detection.
 */

#include <Wire.h>

#define I2C_SLAVE_ADDR   0x20   // must match PCF8574_I2C_ADDR in platformio.ini
#define I2C_SDA_PIN      D2
#define I2C_SCL_PIN      D1
#define NUM_PINS         8

// bit i -> GPIO pin, or -1 if this bit has no physical pin on this board
// (see the pin table in the header comment above for why bit 7 is -1).
const int8_t OUTPUT_PINS[NUM_PINS] = { D0, D3, D4, D5, D6, D7, D8, -1 };

// Boot-time default for THIS rig's own reset, kept LOW to avoid the inrush
// current all the pins driving HIGH at once would cause on whatever is
// wired to them. This is independent of the 'r' handler below, which still
// simulates the real PCF8574's true power-on default (0xFF, idle HIGH) for
// readState() drift testing.
volatile uint8_t last_state = 0x00;

void applyState(uint8_t state) {
  for (uint8_t i = 0; i < NUM_PINS; i++) {
    if (OUTPUT_PINS[i] < 0) continue;   // bit 7: no pin on this board
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
      if (OUTPUT_PINS[i] < 0) {
        Serial.println("    (software-only, no physical pin on this board)");
      }
    }
  }
}

void onI2CRequest() {
  Wire.write(last_state);
}

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < NUM_PINS; i++) {
    if (OUTPUT_PINS[i] < 0) continue;
    pinMode(OUTPUT_PINS[i], OUTPUT);
  }
  applyState(last_state);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_SLAVE_ADDR);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  Serial.println("i2c_actuator_esp8266 ready, listening as PCF8574 stand-in at 0x20");
  Serial.println("Bit 7 has no physical pin on this board (see file header comment)");
  Serial.println("Type 'r' + Enter to simulate the chip losing power (resets to 0xFF)");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') {
      last_state = 0xFF;
      applyState(last_state);
      Serial.println("Simulated reset: chip lost power, all pins HIGH (0xFF)");
    }
  }
}
