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
 * bidirectional pins at once, same as a real PCF8574. A read returns
 * that same byte (see onI2CRequest below), so the companion firmware's
 * readState() can be tested against this rig too.
 *
 * Simulating a power loss on the chip: type 'r' + Enter in the serial
 * monitor to force last_state back to 0xFF (power-on default) without
 * going through onI2CReceive - this mimics the PCF8574 losing power on
 * its own supply rail while the XIAO companion keeps running with a
 * now-stale cached state, so you can test readState()'s drift detection.
 *
 * Active-low relay boards: many commercial relay modules trigger the
 * relay on a LOW input instead of HIGH. Uncomment -D ACTIVE_LOW_RELAYS=1
 * in platformio.ini to invert every physical pin write in applyState().
 * This only flips the GPIO drive level - the I2C protocol byte (what
 * MeshCore's PCF8574Actuator reads back and logs) is untouched, so
 * PIN_STATUS/readState() still see the same 0/1 bits either way.
 * Crucially, this keeps "all pins off" at boot (last_state = 0x00)
 * mapped to the actually-off physical level for the board you have
 * wired - without this flag an active-low board would read
 * last_state=0x00 as "drive every pin LOW", which turns every relay ON
 * at power-up, the exact inrush this default was added to avoid.
 */

#include <Wire.h>

#define I2C_SLAVE_ADDR   0x20   // must match PCF8574_I2C_ADDR in platformio.ini
#define NUM_PINS         8
const uint8_t OUTPUT_PINS[NUM_PINS] = { 2, 3, 4, 5, 6, 7, 8, 9 };

// Boot-time default for THIS rig's own reset, kept LOW to avoid the inrush
// current all 8 pins driving HIGH at once would cause on whatever is wired
// to D2..D9. This is independent of the 'r' handler below, which still
// simulates the real PCF8574's true power-on default (0xFF, idle HIGH) for
// readState() drift testing.
volatile uint8_t last_state = 0x00;

void applyState(uint8_t state) {
  for (uint8_t i = 0; i < NUM_PINS; i++) {
    bool bit_on = state & (1 << i);
#ifdef ACTIVE_LOW_RELAYS
    digitalWrite(OUTPUT_PINS[i], bit_on ? LOW : HIGH);
#else
    digitalWrite(OUTPUT_PINS[i], bit_on ? HIGH : LOW);
#endif
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
      Serial.print((state & (1 << i)) ? "HIGH" : "LOW");
#ifdef ACTIVE_LOW_RELAYS
      Serial.print((state & (1 << i)) ? " (relay ON, GPIO driven LOW)" : " (relay OFF, GPIO driven HIGH)");
#endif
      Serial.println();
    }
  }
}

void onI2CRequest() {
  Wire.write(last_state);
}

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < NUM_PINS; i++) {
    pinMode(OUTPUT_PINS[i], OUTPUT);
  }
  applyState(last_state);

  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  Serial.println("i2c_actuator_nano ready, listening as PCF8574 stand-in at 0x20");
#ifdef ACTIVE_LOW_RELAYS
  Serial.println("ACTIVE_LOW_RELAYS enabled: bit=1 drives GPIO LOW, bit=0 drives GPIO HIGH");
#endif
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
