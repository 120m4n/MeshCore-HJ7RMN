# Sensores I2C (BMP280 / BME280) en modo repeater

Respuesta de referencia a la pregunta recurrente: **¿qué tarjetas traen
soporte de sensores I2C habilitado por defecto en el firmware repeater?**

Verificado contra `platformio.ini` + `variants/*/platformio.ini` en el commit
`5b578581` (rama `feature/i2c-actuator-public-channel`).

## Corrección importante: las XIAO sí lo traen por defecto

Las XIAO nRF52840 y XIAO ESP32-S3 **sí** compilan el soporte BMP280/BME280
por defecto en `Xiao_nrf52_repeater` y `Xiao_S3_repeater`. Las dos secciones
base (`[Xiao_nrf52]` y `[Xiao_S3]`) incluyen `${sensor_base.build_flags}`, y
sus `target.cpp` instancian `EnvironmentSensorManager`. Upstream
(`meshcore-dev/MeshCore`) está igual.

La telemetría vectorial **`tv`** es lo que **no** viene por defecto:

| Qué | ¿Por defecto en el repeater? | Cómo se activa |
| --- | --- | --- |
| Lectura de BMP280/BME280 vía telemetría estándar (CayenneLPP, "Request telemetry" desde la app) | **Sí**, en las dos XIAO | Ya viene en `sensor_base` |
| Ring de 24 h + comandos `tv sensor` / `tv <since>` | **No** | `-D WITH_TV_TELEMETRY=1`. Ya existe como env `Xiao_nrf52_repeater_tv`; la S3 todavía no tiene env `_tv` |

## Funcionalidad en la XIAO nRF52840 (+ Wio-SX1262)

Con el sensor conectado a D6/D7 hay dos caminos que conviven:

1. **Telemetría estándar** (cualquier env que use `sensor_base`).
   `EnvironmentSensorManager` (`src/helpers/sensors/EnvironmentSensorManager.cpp`)
   sondea el bus al arrancar. Busca un BME280 y un BMP280 en `0x76` y en
   `0x77` (por el pin SDO de Bosch). Cada sensor detectado ocupa un canal
   CayenneLPP desde el 2 (el canal 1 es la batería). Responde a la petición
   de telemetría del repeater: temperatura y presión, más humedad si es
   un BME280.
2. **Telemetría vectorial `tv`** (solo `Xiao_nrf52_repeater_tv`).
   `src/helpers/tv_sensor.cpp` detecta una sola vez, en el primer `loop()`,
   un BME280 (chip ID `0x60`) o, si no hay, un BMP280 (`0x58`). Toma una
   muestra cada 30 min y guarda 48 slots (24 h) en RAM. `tv <since>` los
   devuelve como vector v3. Con un BMP280 el campo `H` lleva la presión
   escalada `(hPa-545)/2`. El formato y la paginación están en
   [README_TV_TELEMETRY.md](README_TV_TELEMETRY.md).

> Si al arrancar no se detecta ningún sensor, el modo `tv` no vuelve a
> intentarlo: conectar el sensor en caliente no sirve, hay que reiniciar.

### Pinout XIAO nRF52840: I2C en D6/D7

Los pines I2C "oficiales" de la XIAO (D4/D5) los ocupa el Wio-SX1262
(D4 = NSS, D5 = RXEN), así que el I2C se movió a los pines del UART:

```
                 XIAO nRF52840 (vista superior, USB-C arriba)
                         ┌───[USB-C]───┐
    (libre)     D0  P0.02│●           ●│5V
    LoRa DIO1   D1  P0.03│●           ●│GND ─────────── GND sensor
    LoRa RESET  D2  P0.28│●           ●│3V3 ─────────── VCC sensor
    LoRa BUSY   D3  P0.29│●           ●│D10 P1.15  MOSI (LoRa)
    LoRa NSS    D4  P0.04│●           ●│D9  P1.14  MISO (LoRa)
    LoRa RXEN   D5  P0.05│●           ●│D8  P1.13  SCK  (LoRa)
 ┌─ SCL ───────  D6  P1.11│●           ●│D7  P1.12  ─────── SDA ─┐
 │                       └─────────────┘                        │
 └──────────── al pin SCL del BMP280/BME280   al pin SDA ─────────┘
```

(Las funciones LoRa vienen de `variants/xiao_nrf52/platformio.ini`; los GPIO
de `variants/xiao_nrf52/variant.cpp`.)

| Señal | Pin XIAO | GPIO nRF52840 | Flag |
| --- | --- | --- | --- |
| **SDA** | **D7** | P1.12 (ex-RX) | `-D PIN_WIRE_SDA=D7` |
| **SCL** | **D6** | P1.11 (ex-TX) | `-D PIN_WIRE_SCL=D6` |
| VCC | 3V3 | — | alimentar el sensor a **3.3 V** |
| GND | GND | — | — |

- Bus: `Wire` (TWIM0); no se define `ENV_PIN_SDA`, así que `TELEM_WIRE = &Wire`.
- La mayoría de módulos BMP280/BME280 traen pull-ups de 10 kΩ. Si el tuyo
  no los tiene, agrega 4.7 kΩ a 3V3 en SDA y SCL.
- Para diagnosticar el cableado está `pio run -e Xiao_nrf52_i2c_scan -t upload`,
  un escáner I2C autónomo, sin MeshCore.
- Ojo: que el sensor responda con ACK no significa que esté sano. Valida
  que los valores sean físicamente coherentes.

## Hermana mayor: XIAO ESP32-S3 (+ Wio-SX1262)

Misma idea, mismos pines físicos D6/D7, **pero con SDA y SCL al revés que
en la nRF52840**:

| Señal | Pin XIAO | GPIO ESP32-S3 | Flag |
| --- | --- | --- | --- |
| **SDA** | **D6** | GPIO43 (ex-TX) | `-D PIN_BOARD_SDA=D6` |
| **SCL** | **D7** | GPIO44 (ex-RX) | `-D PIN_BOARD_SCL=D7` |
| VCC | 3V3 | — | 3.3 V |
| GND | GND | — | — |

```
                 XIAO ESP32-S3 (vista superior, USB-C arriba)
                         ┌───[USB-C]───┐
    VBAT read   D0  GPIO1│●           ●│5V
    LoRa DIO1   D1  GPIO2│●           ●│GND ─────────── GND sensor
    LoRa RESET  D2  GPIO3│●           ●│3V3 ─────────── VCC sensor
    LoRa BUSY   D3  GPIO4│●           ●│D10 GPIO9   MOSI (LoRa)
    LoRa NSS    D4  GPIO5│●           ●│D9  GPIO8   MISO (LoRa)
    LoRa RXEN   D5  GPIO6│●           ●│D8  GPIO7   SCK  (LoRa)
 ┌─ SDA ──────  D6 GPIO43│●           ●│D7  GPIO44 ─────── SCL ─┐
 │                       └─────────────┘                        │
 └──────────── al pin SDA del BMP280/BME280   al pin SCL ─────────┘
```

> ⚠️ **Al pasar un sensor de una placa a otra hay que cruzar los cables
> de D6/D7.** En la nRF52840 D6 es SCL; en la S3 D6 es SDA. Si cableas
> igual, el bus queda muerto (todo NACK) y el firmware reporta `none`.

- Bus: `ESP32Board::begin()` hace `Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL)`,
  que es el mismo `Wire` del `EnvironmentSensorManager`.
- GPIO43/44 son el UART0, pero en la S3 los logs salen por USB-CDC, así
  que esos pines quedan libres.
- Existe además la variante `xiao_s3_wio` (`Xiao_S3_WIO_repeater`), que usa
  **D4 = SDA / D5 = SCL** porque su mapeo de LoRa es otro. Revisa qué env
  flasheaste.

## Flags relevantes (las dos XIAO)

Lo que ya viene, heredado de `[sensor_base]` en `platformio.ini`:

```ini
-D ENV_INCLUDE_BME280=1
-D ENV_INCLUDE_BMP280=1
; también AHTX0, SHTC3, SHT4X, LPS22HB, INA3221/219/226/260,
; MLX90614, VL53L0X, BME680, BMP085 (y GPS, que las XIAO quitan con -UENV_INCLUDE_GPS)
```

Lo que puedes agregar o cambiar en el `build_flags` del env:

| Flag | Default | Para qué |
| --- | --- | --- |
| `-D WITH_TV_TELEMETRY=1` | ausente | Activa el ring de 24 h y los comandos `tv`. Para la S3 hay que crear un env `Xiao_S3_repeater_tv` copiando `Xiao_S3_repeater` y sumándole este flag. El código (`tv_sensor.cpp`) usa `&Wire` y no depende de la plataforma, pero **en la S3 no está probado**. |
| `-D TELEM_BMP280_ADDRESS=0x77` / `-D TELEM_BME280_ADDRESS=0x77` | `0x76` | Dirección primaria. Casi nunca hace falta: el manager prueba también la otra. |
| `-D TELEM_BMP280_SEALEVELPRESSURE_HPA=…` / `TELEM_BME280_…` | `1013.25` | Referencia para el cálculo de altitud. |
| `-UENV_INCLUDE_BMP280` o `-D ENV_INCLUDE_BMP280=0` (ídem BME280) | — | Quita el driver, por ejemplo para ahorrar flash. |
| `-D ENV_PIN_SDA=… -D ENV_PIN_SCL=…` | ausente | Mueve los sensores a un **segundo bus** (`Wire1`). Las XIAO no lo usan. |

Envs de la XIAO nRF52840 y si leen el sensor:

| Env | Sensor (telemetría estándar) | `tv` |
| --- | --- | --- |
| `Xiao_nrf52_repeater` | ✅ | ❌ |
| `Xiao_nrf52_repeater_tv` | ✅ | ✅ |
| `Xiao_nrf52_room_server`, `Xiao_nrf52_sensor` | ✅ | ❌ |
| `Xiao_nrf52_companion_radio_*` | ✅ | ❌ |
| `Xiao_nrf52_i2c_scan` | (solo escanea) | ❌ |

## Todas las tarjetas con BMP280/BME280 por defecto en repeater

Criterio: el env `*repeater*` resuelve (siguiendo `extends` y `${…}`)
`ENV_INCLUDE_BMP280` y/o `ENV_INCLUDE_BME280` activos, **y** el `target`
de la variante instancia `EnvironmentSensorManager`. Los pines son los del
bus en el que el manager busca sensores; "variant.h" significa que se toman
del `PIN_WIRE_SDA/SCL` del variant, sin flag explícito.

| Variante (`variants/…`) | BMP280 | BME280 | Bus · SDA/SCL | Envs repeater |
| --- | :-: | :-: | --- | --- |
| `gat562_30s_mesh_kit` | ✅ | ✅ | Wire · 13/14 | `GAT562_30S_Mesh_Kit_repeater` |
| `gat562_mesh_evb_pro` | ✅ | ✅ | Wire · 13/14 | `GAT562_Mesh_EVB_Pro_repeater` |
| `gat562_mesh_tracker_pro` | ✅ | ✅ | Wire · 13/14 | `GAT562_Mesh_Tracker_Pro_repeater` |
| `heltec_rc32` | ✅ | ✅ | Wire · 21/18 | `heltec_rc32_repeater`, `heltec_rc32_without_display_repeater` (+ `_bridge_espnow`) |
| `heltec_t096` | ✅ | ✅ | Wire · variant.h | `Heltec_t096_repeater`, `…_bridge_rs232` |
| `heltec_t1` | ✅ | ✅ | Wire · variant.h | `Heltec_t1_repeater` |
| `heltec_t114` | ✅ | ✅ | **Wire1** · `PIN_WIRE1_SDA/SCL` | `Heltec_t114_repeater`, `Heltec_t114_without_display_repeater` (+ `_bridge_rs232`) |
| `heltec_tracker_v2` | ✅ | ✅ | Wire · 6/17 | `heltec_tracker_v2_repeater`, `…_bridge_espnow` |
| `heltec_v3` | ✅ | ✅ | `Heltec_v3_repeater`: **Wire1 · 33/34** (header inferior). El resto: Wire · 17/18 ⚠️ bus del OLED, solo pads traseros (ver nota abajo) | `Heltec_v3_repeater`, `Heltec_WSL3_repeater` (+ `_bridge_espnow`, `_bridge_rs232`) |
| `heltec_v4` | ✅ | ✅ | Wire · 17/18 (OLED), 4/3 (TFT); expansion kit: **Wire1** · 4/3 | `heltec_v4_repeater`, `heltec_v4_tft_repeater` (+ `_bridge_espnow`), `heltec_v4_expansionkit_repeater` |
| `heltec_v4_r8` | ✅ | ✅ | Wire · 17/18 | `heltec_v4_r8_repeater`, `heltec_v4_r8_tft_repeater` |
| `ikoka_handheld_nrf` | ✅ | ✅ | Wire · variant.h | `ikoka_handheld_nrf_e22_30dbm_repeater` |
| `ikoka_nano_nrf` | ✅ | ✅ | Wire · 4/5 | `ikoka_nano_nrf_{22,30,33}dbm_repeater` |
| `ikoka_stick_nrf` | ✅ | ✅ | Wire · 6/7 | `ikoka_stick_nrf_{22,30,33}dbm_repeater` |
| `lilygo_t_impulse_plus` | ✅ | ✅ | **Wire1** · `IIC_SDA_2/IIC_SCL_2` | `LilyGo_T_Impulse_Plus_repeater` |
| `lilygo_tbeam_supreme_SX1262` | ❌ | ✅ | Wire · 17/18 | `T_Beam_S3_Supreme_SX1262_repeater`, `…_bridge_espnow` |
| `lilygo_techo` | ❌ | ✅ | Wire · variant.h | `LilyGo_T-Echo_repeater` |
| `lilygo_tlora_v2_1` | ✅ | ✅ | Wire · 21/22 | `LilyGo_TLora_V2_1_1_6_repeater` (+ `_bridge_espnow`, `_bridge_rs232`) |
| `meshnology_w12` | ✅ | ✅ | Wire · 17/18 | `meshnology_w12_repeater`, `…_bridge_espnow` |
| `minewsemi_me25ls01` | ❌ | ✅ | Wire · variant.h | `Minewsemi_me25ls01_repeater` |
| `muziworks_r1_neo` | ✅ | ✅ | Wire · variant.h | `R1Neo_repeater` |
| `promicro` | ✅ | ✅ | Wire · 8/7 | `ProMicro_repeater`, `…_bridge_rs232_serial1` |
| `rak3112` | ✅ | ✅ | Wire · 9/40 | `RAK_3112_repeater` (+ `_bridge_espnow`, `_bridge_rs232`) |
| `rak3401` | ✅ | ✅ | Wire · variant.h | `RAK_3401_repeater` |
| `rak4631` | ✅ | ✅ | Wire · 13/14 | `RAK_4631_repeater`, `…_ethernet`, `…_bridge_rs232_serial{1,2}` |
| `rak_wismesh_tag` | ✅ | ✅ | Wire · variant.h | `RAK_WisMesh_Tag_repeater` |
| `sensecap_solar` | ✅ | ✅ | Wire · variant.h | `SenseCap_Solar_repeater` |
| `station_g2` | ✅ | ✅ | Wire · 5/6 | `Station_G2_repeater`, `Station_G2_logging_repeater` (+ `_bridge_espnow`) |
| `station_g3_esp32` | ✅ | ✅ | Wire · 5/6 | `Station_G3_ESP32_repeater`, `…_logging_repeater` |
| `thinknode_m6` | ✅ | ✅ | Wire · variant.h | `ThinkNode_M6_repeater` |
| `thinknode_m9` | ✅ | ✅ | Wire · 7/6 | `ThinkNode_M9_repeater_` |
| `wio-tracker-l1` | ✅ | ✅ | Wire · variant.h | `WioTrackerL1_repeater` |
| `wio-tracker-l1-1w` | ✅ | ✅ | Wire · variant.h | `WioTrackerL1-1W_repeater` |
| `wio_wm1110` | ✅ | ✅ | Wire · variant.h | `wio_wm1110_repeater` |
| `xiao_c3` | ✅ | ✅ | Wire · D6/D7 | `Xiao_C3_repeater` |
| **`xiao_nrf52`** | ✅ | ✅ | Wire · **D7/D6** | `Xiao_nrf52_repeater`, `Xiao_nrf52_repeater_tv` |
| **`xiao_s3`** | ✅ | ✅ | Wire · **D6/D7** | `Xiao_S3_repeater`, `…_bridge_espnow` |
| `xiao_s3_wio` | ✅ | ✅ | Wire · D4/D5 | `Xiao_S3_WIO_repeater`, `…_bridge_espnow` |

> ⚠️ **"Bus · SDA/SCL" es el bus que el firmware sondea, no un pin que
> esté necesariamente disponible en el header.** En muchas placas con
> pantalla, `PIN_BOARD_SDA/SCL` es el bus interno del OLED/RTC/PMU. El
> sensor funciona si lo cuelgas de ese bus (el OLED SSD1306 está en `0x3C`
> y no choca con `0x76`/`0x77`), pero si los pines no salen al header hay
> que soldar. Antes de cablear, contrasta con el pinout físico de la placa.
>
> **Caso confirmado con foto: Heltec WiFi LoRa 32 V3 (PCB `HTIT-WB32LAF`).**
> GPIO17/18 son SDA/SCL del OLED y **no salen a los headers**. Esto es lo
> que dice la serigrafía:
>
> ```
> Header superior: 7  6  5  4  3  2  1  38 39 40 41 42 45 46 37 3V3 3V3 GND
> Header inferior: 19 20 21 26 48 47 33 34 35 36 0  RST TX RX Ve Ve  5V  GND
> Pads sueltos:    "17" "18"  (cara trasera, junto al cristal DA330C,
>                              sobre "www.heltec.cn")
> ```
>
> **`Heltec_v3_repeater` ya viene resuelto en este fork:** su env define
> `-D ENV_PIN_SDA=33 -D ENV_PIN_SCL=34`, igual que `Heltec_v3_sensor`. Los
> sensores van en `Wire1`: cableas **SDA → `33`** y **SCL → `34`** en el
> header inferior. Al usar `Wire1`, los pads 17/18 ya **no** se sondean
> para sensores.
>
> Los demás envs de la V3 (`_bridge_*` y los `Heltec_WSL3_*`) siguen en
> 17/18. Para usar un sensor con ellos, suelda a los pads traseros `17`
> (SDA) y `18` (SCL), o agrégales las mismas dos líneas.
>
> Las placas Heltec que usan 17/18 (`heltec_v4`, `heltec_v4_r8`) y
> `meshnology_w12` probablemente tienen el mismo problema; no lo he
> verificado contra su pinout físico.

Solo `lilygo_tbeam_supreme_SX1262`, `lilygo_techo` y `minewsemi_me25ls01`
declaran BME280 por su cuenta, sin `sensor_base`. Por eso no traen el
driver BMP280: un BMP280 conectado a esas placas **no se detecta**.

### Casos raros (no cuentan como "por defecto")

- `m5stack_unit_c6l`: incluye `sensor_base`, pero su target usa el
  `SensorManager` genérico, no el `EnvironmentSensorManager`, así que los
  flags no tienen efecto.
- `lilygo_tdeck`: incluye `sensor_base`, pero lo anula explícitamente con
  `ENV_INCLUDE_BME280=0` / `ENV_INCLUDE_BMP280=0`.
- `muzi_base`: solo trae las `lib_deps` de `sensor_base`, no los flags.

### Sin BMP280/BME280 en su repeater

`ebyte_eora_s3`, `generic-e22`, `heltec_ct62`, `heltec_e213`, `heltec_e290`,
`heltec_mesh_solar`, `heltec_t190`, `heltec_tower_v2`, `heltec_tracker`,
`heltec_v2`, `heltec_wireless_paper`, `keepteen_lt1`, `lilygo_t3s3`,
`lilygo_t3s3_sx1276`, `lilygo_tbeam_1w`, `lilygo_tbeam_SX1262`,
`lilygo_tbeam_SX1276`, `lilygo_tdeck`, `lilygo_techo_card`,
`lilygo_techo_lite`, `lilygo_teth_elite`, `lilygo_tlora_c6`, `mesh_pocket`,
`meshadventurer`, `meshtracker_x1`, `muzi_base`, `nano_g2_ultra`,
`nibble_screen_connect`, `nibble_zero_connect`, `rak11310`, `rak3x72`,
`rpi_picow`, `t1000-e`, `tenstar_c3`, `thinknode_m1`, `m2`, `m3`, `m4`,
`m5`, `m7`, `m8`, `tiny_relay`, `waveshare_esp32s3_lr1121`,
`waveshare_rp2040_lora`, `wio-e5-dev`, `wio-e5-mini`, `xiao_c6`,
`xiao_rp2040`.

Para habilitarlo en una de estas, agrega al env `${sensor_base.build_flags}`
y `${sensor_base.lib_deps}`, más `+<helpers/sensors>` en `build_src_filter`.
El target tiene que usar `EnvironmentSensorManager`.

### Cómo re-verificar una tarjeta

```bash
grep -n "sensor_base\|ENV_INCLUDE_BM[EP]280" variants/<placa>/platformio.ini
grep -n "EnvironmentSensorManager" variants/<placa>/target.cpp
```
