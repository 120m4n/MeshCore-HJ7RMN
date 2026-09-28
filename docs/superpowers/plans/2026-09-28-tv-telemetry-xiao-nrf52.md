# TV Telemetry (XIAO nRF52 repeater) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 24h RAM ring buffer + `tv <since>` command to the XIAO nRF52 repeater firmware, encoding temperature/humidity samples as compact anchor+delta integer vectors, in a new `Xiao_nrf52_repeater_tv` PlatformIO env.

**Architecture:** A header-only encoder (`tv_telemetry.h`, pure C++/stdlib, no Arduino deps, unit-testable under `env:native`) owns the ring buffer and the `T,H,t;dT,dH,dt;...` encoding. A small adapter (`tv_sensor.h/.cpp`) probes the I2C bus once at boot for a BME280/BMP280 and supplies the integer sensor readings the encoder needs. Both are wired into `examples/simple_repeater/MyMesh.cpp` behind a single `WITH_TV_TELEMETRY` build flag so every other board/env that shares this same `MyMesh.cpp` file is unaffected.

**Tech Stack:** Arduino/PlatformIO (nRF52), C++17 (encoder is also compiled under PlatformIO's `env:native` + GoogleTest), Adafruit BME280/BMP280 libraries (already a `sensor_base` dependency for all `Xiao_nrf52_*` envs).

**Spec:** `docs/Spec telemetría vectorial MeshCore (XIAO nRF52).md`

## Global Constraints

- Muestreo: cada 15 min (96 slots por día).
- Retención: 1 día, ring en RAM (~768 bytes), sin persistencia — un reinicio borra el buffer.
- Formato: anchor absoluto + deltas contra el anchor (nunca contra el registro anterior); `T` = décimas de °C, `H` = %RH entero 0–100, `t` = epoch en minutos UTC.
- El encoder de firmware (`tv_telemetry.h`) usa solo enteros: sin floats, sin filesystem.
- Consulta `tv <since>`: una respuesta = un paquete; `"-"` literal si no hay datos nuevos; un vector nunca se parte entre paquetes.
- **Desviación pedida por el usuario respecto al spec original:** el muestreo NO depende de `clock sync` — usa el reloj interno de la XIAO esté o no sincronizado (se elimina el parámetro `clock_ok` de `sample_tick`).
- **Desviación pedida por el usuario respecto al spec original:** el muestreo arranca **si y solo si** se detecta un sensor I2C BME280 o BMP280 en el bus al boot; si no se detecta ninguno, el muestreo **nunca** arranca (la detección se cachea una sola vez, no se reintenta en cada loop).
- Todo el código nuevo va detrás de `#ifdef WITH_TV_TELEMETRY`, definido solo en el nuevo env `Xiao_nrf52_repeater_tv`, porque `examples/simple_repeater/MyMesh.cpp` es compartido por los envs `_repeater` de *todas* las variantes de placa, no solo XIAO nRF52.

## Review Focus

- `cap` menor a 32 (buffer de respuesta demasiado chico para ni siquiera un anchor; no debería pasar con `TV_REPLY_CAP=150`, pero `encode()` debe fallar limpio igual) → devuelve `0` sin escribir nada en `out`, nunca escribe fuera de rango.
- `since` posterior al epoch del último registro disponible → respuesta `"-"`, no un error.
- Un slot con `now_min - epoch_min >= 1440` (más viejo que 24h, o slot en cero) nunca debe aparecer en la respuesta, ni siquiera con `since=0`.
- Truncamiento por payload lleno: si no caben todos los registros pendientes, la respuesta corta limpio (nunca parte un registro a la mitad) y trae los más viejos primero.
- Sin sensor BME280/BMP280 detectado al boot: `tv <since>` debe responder siempre `"-"`, nunca debe re-intentar detectar el sensor en cada vuelta de loop, y nunca debe crashear si no hay sensor.

---

### Task 1: Ring buffer + vector encoder (`tv_telemetry.h`)

**Files:**
- Create: `src/helpers/tv_telemetry.h`
- Test: `test/test_tv_telemetry/test_tv_telemetry.cpp`

**Interfaces:**
- Produces: `tv::INTERVAL_MIN`, `tv::SLOTS`, `tv::Rec`, `tv::ring` (internal-linkage array, directly accessible from a test TU that includes the header), `tv::put(uint32_t now_min, int16_t t, uint8_t h)`, `tv::valid(const Rec&, uint32_t now_min) -> bool`, `tv::encode(uint32_t now_min, uint32_t since, char* out, size_t cap) -> size_t`, `tv::handle_tv(const char* args, uint32_t now_min, char* reply, size_t cap)`, `tv::sample_tick(uint32_t now_min)`.
- Consumes (declared here, defined in Task 2): `bool tv::sensor_ready()`, `int16_t tv::read_temp_dC()`, `uint8_t tv::read_hum_pct()`.

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_tv_telemetry/test_tv_telemetry.cpp
#include <gtest/gtest.h>
#include "../../src/helpers/tv_telemetry.h"

class TvTelemetry : public ::testing::Test {
protected:
  void SetUp() override {
    memset(tv::ring, 0, sizeof(tv::ring));
  }
};

TEST_F(TvTelemetry, MatchesSpecReferenceVector) {
  // Ejemplo de referencia de la spec: anchor 07:13 UTC, epoch_min=29840113
  tv::put(29840113, 330, 65);
  tv::put(29840174, 318, 68);   // dT=-12 dH=+3 dt=61
  tv::put(29840189, 305, 73);   // dT=-25 dH=+8 dt=76

  char out[64];
  size_t n = tv::encode(29840189, 0, out, sizeof(out));

  EXPECT_STREQ("330,65,29840113;-12,3,61;-25,8,76", out);
  EXPECT_EQ(strlen(out), n);
}

TEST_F(TvTelemetry, NoNewDataReturnsDash) {
  tv::put(1000, 200, 50);

  char out[64];
  size_t n = tv::encode(1000, 1000, out, sizeof(out));  // since == last epoch

  EXPECT_STREQ("-", out);
  EXPECT_EQ(1u, n);
}

TEST_F(TvTelemetry, EmptyRingReturnsDash) {
  char out[64];
  size_t n = tv::encode(5000, 0, out, sizeof(out));

  EXPECT_STREQ("-", out);
  EXPECT_EQ(1u, n);
}

TEST_F(TvTelemetry, CapBelow32ReturnsZeroWithoutTouchingBuffer) {
  tv::put(1000, 200, 50);

  char out[8] = {0};
  size_t n = tv::encode(1000, 0, out, 10);  // cap < 32 short-circuits before writing anything

  EXPECT_EQ(0u, n);
  EXPECT_STREQ("", out);  // untouched: caller must not send this as a reply
}

TEST_F(TvTelemetry, StaleSlotOlderThanADayIsExcluded) {
  uint32_t now_min = 100000;
  tv::put(now_min - 1440, 111, 40);  // exactly 1 day old: excluded (< 1440 required, not <=)
  tv::put(now_min - 100, 222, 45);   // fresh: included

  char out[64];
  size_t n = tv::encode(now_min, 0, out, sizeof(out));

  char expected[32];
  snprintf(expected, sizeof(expected), "222,45,%u", (unsigned)(now_min - 100));
  EXPECT_STREQ(expected, out);
  EXPECT_EQ(strlen(out), n);
}

TEST_F(TvTelemetry, TruncatesCleanlyWhenPacketIsFull) {
  tv::put(200000, 100, 50);       // anchor: "100,50,200000" (13 bytes)
  tv::put(200015, 105, 51);       // delta:  ";5,1,15" (7 bytes) -> cumulative 20, fits cap=32
  tv::put(200030, -12345, 52);    // delta:  ";-12450,1,15" (12 bytes) -> cumulative 32, overflows cap=32

  char out[64];
  size_t n = tv::encode(200030, 0, out, 32);  // cap is the encoder's documented minimum

  EXPECT_STREQ("100,50,200000;5,1,15", out);  // third record excluded whole, never split
  EXPECT_EQ(strlen(out), n);
}

TEST_F(TvTelemetry, FullRingEncodesOldestValidSlotsFirst) {
  // Llena las 96 slots; encode() debe recorrerlas en orden cronológico
  // ascendente empezando por la más vieja, sin depender del orden de escritura.
  uint32_t now_min = 500000 - (500000 % tv::INTERVAL_MIN);  // alineado a slot
  for (uint32_t i = 0; i < tv::SLOTS; i++) {
    uint32_t epoch = now_min - (tv::SLOTS - 1 - i) * tv::INTERVAL_MIN;
    tv::put(epoch, (int16_t)(200 + i), (uint8_t)(40 + (i % 50)));
  }

  char out[128];
  size_t n = tv::encode(now_min, 0, out, sizeof(out));

  uint32_t oldest_epoch = now_min - (tv::SLOTS - 1) * tv::INTERVAL_MIN;
  char expected_prefix[24];
  snprintf(expected_prefix, sizeof(expected_prefix), "200,40,%u", (unsigned)oldest_epoch);
  EXPECT_EQ(0, strncmp(expected_prefix, out, strlen(expected_prefix)));  // oldest slot leads
  EXPECT_EQ(strlen(out), n);
}

TEST_F(TvTelemetry, HandleTvParsesSinceAndEncodes) {
  tv::put(9000, 10, 20);

  char reply[64];
  tv::handle_tv("0", 9000, reply, sizeof(reply));
  EXPECT_STREQ("10,20,9000", reply);

  tv::handle_tv("9000", 9000, reply, sizeof(reply));
  EXPECT_STREQ("-", reply);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_tv_telemetry`
Expected: FAIL to compile — `src/helpers/tv_telemetry.h` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/helpers/tv_telemetry.h — ring de 24 h + encoder de vectores "T,H,t;dT,dH,dt;..."
//
// Desviaciones respecto al doc de spec original (pedidas explícitamente):
//  - sample_tick() NO recibe/chequea clock_ok: se muestrea con el reloj interno
//    de la XIAO esté o no sincronizado.
//  - sample_tick() solo escribe si tv::sensor_ready() (ver tv_sensor.h) devuelve
//    true; esa función cachea el resultado de detectar BME280/BMP280 una sola
//    vez al boot y nunca reintenta si no se encontró nada.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace tv {

constexpr uint32_t INTERVAL_MIN = 15;
constexpr uint32_t SLOTS = 1440 / INTERVAL_MIN;  // 96

struct Rec {
  uint32_t epoch_min;  // 0 = slot vacío
  int16_t  t;          // décimas de °C
  uint8_t  h;          // %RH
};

static Rec ring[SLOTS];  // en BSS: arranca en cero

inline void put(uint32_t now_min, int16_t t, uint8_t h) {
  ring[(now_min / INTERVAL_MIN) % SLOTS] = { now_min, t, h };
}

inline bool valid(const Rec& r, uint32_t now_min) {
  return r.epoch_min != 0 && r.epoch_min <= now_min && (now_min - r.epoch_min) < 1440;
}

// Ganchos del sensor: implementados en tv_sensor.cpp (ver Task 2).
// sensor_ready() se cachea la primera vez que se llama y nunca vuelve a
// escanear el bus si no encontró nada soportado.
bool sensor_ready();
int16_t read_temp_dC();
uint8_t read_hum_pct();

// Llamar desde loop(). No muestrea si no hay sensor soportado detectado.
inline void sample_tick(uint32_t now_min) {
  static uint32_t last_bucket = 0;
  if (!sensor_ready()) return;
  uint32_t b = now_min / INTERVAL_MIN;
  if (b == last_bucket) return;
  last_bucket = b;
  put(now_min, read_temp_dC(), read_hum_pct());
}

// Escribe en `out` (terminado en NUL) los registros con epoch_min > since,
// del más viejo al más nuevo, sin partir ningún registro. `cap` incluye el NUL
// y debe ser >= 32. Sin datos nuevos escribe "-". Devuelve el largo escrito.
inline size_t encode(uint32_t now_min, uint32_t since, char* out, size_t cap) {
  if (cap < 32) return 0;
  size_t n = 0;
  bool first = true;
  int16_t t0 = 0;
  uint8_t h0 = 0;
  uint32_t e0 = 0;
  const uint32_t start = (now_min / INTERVAL_MIN + 1) % SLOTS;  // slot más viejo

  for (uint32_t i = 0; i < SLOTS; i++) {
    const Rec& r = ring[(start + i) % SLOTS];
    if (!valid(r, now_min) || r.epoch_min <= since) continue;

    char buf[32];
    int len;
    if (first) {
      len = snprintf(buf, sizeof buf, "%d,%u,%lu",
                     (int)r.t, (unsigned)r.h, (unsigned long)r.epoch_min);
    } else {
      len = snprintf(buf, sizeof buf, ";%d,%d,%lu",
                     (int)r.t - (int)t0, (int)r.h - (int)h0,
                     (unsigned long)(r.epoch_min - e0));
    }
    if (len <= 0 || n + (size_t)len >= cap) break;  // no cabe: el resto va en la próxima consulta

    memcpy(out + n, buf, (size_t)len);
    n += (size_t)len;
    if (first) { t0 = r.t; h0 = r.h; e0 = r.epoch_min; first = false; }
  }

  if (first) { out[0] = '-'; out[1] = '\0'; return 1; }
  out[n] = '\0';
  return n;
}

// Gancho del comando remoto "tv <since>"; `args` apunta a lo que sigue a "tv ".
inline void handle_tv(const char* args, uint32_t now_min, char* reply, size_t cap) {
  uint32_t since = (uint32_t)strtoul(args, nullptr, 10);  // vacío o 0 = todo
  encode(now_min, since, reply, cap);
}

}  // namespace tv
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_tv_telemetry`
Expected: PASS (8 tests).

- [ ] **Step 5: Commit**

```bash
git add src/helpers/tv_telemetry.h test/test_tv_telemetry/test_tv_telemetry.cpp
git commit -m "feat: add tv telemetry ring buffer and vector encoder"
```

---

### Task 2: Sensor adapter (`tv_sensor.h/.cpp`) — BME280/BMP280 probe-once

**Files:**
- Create: `src/helpers/tv_sensor.h`
- Create: `src/helpers/tv_sensor.cpp`

**Interfaces:**
- Consumes: Adafruit_BME280, Adafruit_BMP280 (already `sensor_base` lib deps on every `Xiao_nrf52_*` env), global `Wire`.
- Produces: `bool tv::sensor_ready()`, `int16_t tv::read_temp_dC()`, `uint8_t tv::read_hum_pct()` — matching the declarations consumed by `tv_telemetry.h` in Task 1.

This file is Arduino/I2C-hardware dependent (no host-side unit test is meaningful here — see ponytail note on hardware). It compiles for every `Xiao_nrf52_*` env (because `src/helpers/*.cpp` is already picked up by the shared `build_src_filter`), so its entire body is wrapped in `#ifdef WITH_TV_TELEMETRY` to stay a no-op everywhere except the new `Xiao_nrf52_repeater_tv` env.

- [ ] **Step 1: Write the header**

```cpp
// src/helpers/tv_sensor.h
#pragma once
#include <stdint.h>

namespace tv {
bool sensor_ready();
int16_t read_temp_dC();
uint8_t read_hum_pct();
}
```

- [ ] **Step 2: Write the implementation**

```cpp
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

}  // namespace tv

#endif // WITH_TV_TELEMETRY
```

- [ ] **Step 3: Commit**

```bash
git add src/helpers/tv_sensor.h src/helpers/tv_sensor.cpp
git commit -m "feat: add BME280/BMP280 probe-once adapter for tv telemetry"
```

---

### Task 3: Wire `tv <since>` command + sampling loop into the repeater

**Files:**
- Modify: `examples/simple_repeater/MyMesh.cpp`

**Interfaces:**
- Consumes: `tv::handle_tv(const char*, uint32_t, char*, size_t)` and `tv::sample_tick(uint32_t)` from Task 1; `getRTCClock()->getCurrentTime()` (already available on `mesh::Mesh`, `MyMesh`'s base class, per `src/Mesh.h:183`).
- Produces: nothing new consumed elsewhere — this is the outermost integration point.

`examples/simple_repeater/MyMesh.cpp` is shared by every board variant's `_repeater` env, so every addition here is wrapped in `#ifdef WITH_TV_TELEMETRY` (only defined by the new `Xiao_nrf52_repeater_tv` env from Task 4) to leave all other builds byte-for-byte unchanged.

- [ ] **Step 1: Add the include and the reply-cap constant near the top of the file**

Add right after the existing `#include "MyMesh.h"` line:

```cpp
#include "MyMesh.h"

#ifdef WITH_TV_TELEMETRY
#include <helpers/tv_telemetry.h>
// MyMesh::onPeerDataRecv() replies into `uint8_t temp[166]; reply = &temp[5]`,
// i.e. a real cap of 161 bytes (NUL included). Leave margin for the optional
// 3-byte companion-radio CLI prefix reflection in handleCommand() below.
#define TV_REPLY_CAP 150
#endif
```

- [ ] **Step 2: Add the `tv <since>` branch in `MyMesh::handleCommand`**

In `MyMesh::handleCommand` (around line 1273, right before the final `} else{ _cli.handleCommand(...); }`), insert:

```cpp
  } else if (memcmp(command, "discover.neighbors", 18) == 0) {
    const char* sub = command + 18;
    while (*sub == ' ') sub++;
    if (*sub != 0) {
      strcpy(reply, "Err - discover.neighbors has no options");
    } else {
      sendNodeDiscoverReq();
      strcpy(reply, "OK - Discover sent");
    }
#ifdef WITH_TV_TELEMETRY
  } else if (memcmp(command, "tv ", 3) == 0) {
    tv::handle_tv(command + 3, getRTCClock()->getCurrentTime() / 60, reply, TV_REPLY_CAP);
#endif
  } else{
    _cli.handleCommand(sender_timestamp, command, reply);  // common CLI commands
  }
```

(Only the new `#ifdef WITH_TV_TELEMETRY` block is new; the surrounding `discover.neighbors` and final `else` branches already exist and must not otherwise change.)

- [ ] **Step 3: Call `sample_tick()` from `MyMesh::loop()`**

At the very top of `MyMesh::loop()` (before the existing `#ifdef WITH_BRIDGE` block):

```cpp
void MyMesh::loop() {
#ifdef WITH_TV_TELEMETRY
  tv::sample_tick(getRTCClock()->getCurrentTime() / 60);
#endif
#ifdef WITH_BRIDGE
  bridge.loop();
#endif

  mesh::Mesh::loop();
  ...
```

- [ ] **Step 4: Verify the untouched envs still build**

Run: `pio run -e Xiao_nrf52_repeater`
Expected: builds successfully, unchanged behaviour (flag not defined, all new code compiled out).

- [ ] **Step 5: Commit**

```bash
git add examples/simple_repeater/MyMesh.cpp
git commit -m "feat: wire tv <since> command and sampling loop into simple_repeater"
```

---

### Task 4: New PlatformIO env `Xiao_nrf52_repeater_tv`

**Files:**
- Modify: `variants/xiao_nrf52/platformio.ini`

**Interfaces:**
- Consumes: `[Xiao_nrf52]` base env (already defined in this file), `WITH_TV_TELEMETRY` flag consumed by Task 2/3 code.
- Produces: buildable env `Xiao_nrf52_repeater_tv`.

- [ ] **Step 1: Add the new env**

Insert right after the existing `[env:Xiao_nrf52_repeater]` block (which stays completely unchanged):

```ini
[env:Xiao_nrf52_repeater_tv]
extends = Xiao_nrf52
build_flags =
  ${Xiao_nrf52.build_flags}
  -D ADVERT_NAME='"Xiao_nrf52 Repeater TV"'
  -D ADVERT_LAT=0.0
  -D ADVERT_LON=0.0
  -D ADMIN_PASSWORD='"password"'
  -D MAX_NEIGHBOURS=50
  -D WITH_TV_TELEMETRY=1
;  -D MESH_PACKET_LOGGING=1
;  -D MESH_DEBUG=1
build_src_filter = ${Xiao_nrf52.build_src_filter}
  +<../examples/simple_repeater/*.cpp>
```

- [ ] **Step 2: Build it**

Run: `pio run -e Xiao_nrf52_repeater_tv`
Expected: BUILD SUCCESS, and the linked firmware includes `tv_telemetry.h`'s `encode()`/`handle_tv()` and `tv_sensor.cpp`'s BME280/BMP280 probing.

- [ ] **Step 3: Commit**

```bash
git add variants/xiao_nrf52/platformio.ini
git commit -m "feat: add Xiao_nrf52_repeater_tv platformio env"
```

---

## Manual hardware verification (not automatable, do after Task 4 is flashed)

- With no I2C sensor wired: `tv 0` must always reply `-`.
- With a BME280 wired at `0x76`: after ~15 min uptime (first sample bucket), `tv 0` must reply a single anchor record; after ~30 min, `tv <since>` with the previous epoch must reply exactly one delta record.
- `clock` may be unsynced (never ran `clock sync`) and sampling must still occur — confirms the removed `clock_ok` gate behaves as the user requested.
