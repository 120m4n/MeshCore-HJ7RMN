# Telemetría de sensores I2C nativos (XIAO nRF52840 + Wio-SX1262)

Variante del firmware companion de MeshCore que reporta sensores
ambientales I2C **nativos** (chips con interfaz I2C real, sin necesidad
de puentear un protocolo de otro tipo a través de un segundo
microcontrolador) como parte de la telemetría formal del nodo
(`GetTelemetry`), usando el framework `EnvironmentSensorManager` ya
integrado en `companion_radio`.

- **Target de hardware**: Seeed XIAO nRF52840 + módulo LoRa Wio-SX1262
  (entorno PlatformIO `variants/xiao_nrf52`), mismo bus I2C (`D6`/`D7`)
  que las demás variantes I2C de este directorio.
- **Firmwares**: `Xiao_nrf52_companion_radio_usb_i2c_sensors` y
  `Xiao_nrf52_companion_radio_ble_i2c_sensors` — variantes limpias,
  **sin** el actuador PCF8574 (ver "Por qué una variante separada" más
  abajo).
- **Framework**: `src/helpers/sensors/EnvironmentSensorManager.h/.cpp` —
  ya usado por `companion_radio` para responder `GetTelemetry`; esta
  variante no le agrega código nuevo, solo lo activa sin las
  características de I2C que no aplican aquí.
- **Sensor probado**: BME280 (temperatura, humedad, presión, altitud),
  dirección I2C `0x76`. El mismo mecanismo soporta, sin cambios de
  código adicionales, cualquiera de los demás sensores que ya trae
  `EnvironmentSensorManager`: AHT10/AHT20, BMP280, SHTC3, SHT4X,
  LPS22HB, INA3221/INA219/INA226/INA260, MLX90614, VL53L0X, BMP085 — con
  solo cablear el chip correspondiente al bus, sin tocar firmware.

## Cómo funciona

1. Al arrancar, `EnvironmentSensorManager::begin()` escanea el bus I2C
   completo (direcciones `0x08`-`0x77`) y anota qué direcciones
   responden — **antes** de tocar ninguna librería de sensor, así un
   chip ausente o defectuoso no puede colgar el arranque.
2. Para cada dirección detectada que coincide con la tabla interna de
   sensores soportados (`SENSOR_TABLE[]`), inicializa el driver
   correspondiente y lo registra como sensor activo — esto es el
   "mapeo" antes de interrogar: solo se interroga lo que realmente
   respondió en el escaneo, nunca a ciegas.
3. Cada vez que el nodo responde una petición de telemetría (dos casos,
   ver abajo), `sensors.querySensors()` recorre los sensores activos y
   agrega sus lecturas al buffer CayenneLPP de la respuesta — la
   petición de telemetría entrante no dispara ningún mapeo nuevo, solo
   usa lo que ya se detectó en el arranque.

### Dos formas de pedir telemetría (ambas incluyen los sensores I2C)

- **Consulta local de la app companion** (`CMD_SEND_TELEMETRY_REQ`,
  `MyMesh.cpp:1859`): la app pide la telemetría de su propio nodo por
  USB/BLE. Siempre devuelve todo (`sensors.querySensors(0xFF, ...)`),
  sin restricción de permisos — es el dueño del nodo preguntando por su
  propio hardware.
- **Petición de otro nodo por la malla** (`REQ_TYPE_GET_TELEMETRY_DATA`,
  `MyMesh.cpp:830`): un nodo remoto (ej. otro companion, o un cliente
  admin) pide la telemetría de este nodo. Aquí sí aplica un control de
  permisos por tipo de dato:

  | Preferencia (`tel_base`/`tel_loc`/`tel_env`) | `0` (deny) | `1` (allow_flags) | `2` (allow_all) |
  |---|---|---|---|
  | Efecto | nunca se incluye ese tipo de dato a peticiones remotas | solo si el contacto que pregunta tiene el flag correspondiente activado | siempre se incluye a cualquier peticionario remoto |

  Los sensores I2C ambientales (BME280 y compañía) caen bajo
  `tel_env`, cuyo valor por defecto es `0` (deny) — **por defecto, un
  nodo remoto no recibe estos datos** aunque el sensor esté cableado y
  funcionando; hay que subirlo explícitamente:

  ```
  set tel_env 2
  ```

  (vía la CLI admin del nodo, o el equivalente en la app companion:
  ajustes de telemetría → ambiental). `2` = allow_all; `1` = solo a
  contactos marcados; `0` = nunca a peticiones remotas (pero la app
  local del propio dueño siempre ve todo, sin importar este valor).

## Por qué una variante separada

`ENV_INCLUDE_BME280=1` (y el resto del set `ENV_INCLUDE_*`) ya viene
heredado por **todos** los envs `Xiao_nrf52_*` vía `sensor_base`,
incluidos los `companion_radio_usb`/`_ble` normales — cablear un BME280
ahí también funcionaría sin cambios. Esta variante existe para tener un
build limpio, dedicado solo a sensores I2C nativos, sin el actuador
PCF8574 (`HAS_PCF8574_ACTUATOR`) — evita mezclar el modelo de "comando
de canal" (ver `README_I2C.md`) con el de telemetría formal en el mismo
build.

## Conexión física (BME280)

Mismo bus I2C ya inicializado por el firmware:

| Señal | Pin XIAO | Pin BME280 |
|-------|----------|------------|
| SDA   | D7       | SDA        |
| SCL   | D6       | SCL        |
| —     | 3.3V     | VCC        |
| —     | GND      | GND        |

- Dirección I2C: `0x76` (algunos módulos usan `0x77` si el pin `SDO` va
  a VCC — no soportado por defecto; si tu módulo la usa, ajusta
  `TELEM_BME280_ADDRESS` como build flag).
- Pull-ups en SDA/SCL (4.7kΩ típico) si tu módulo no las trae
  integradas — la mayoría de breakouts BME280 ya las incluyen.

## Compilar y flashear

```sh
pio run -e Xiao_nrf52_companion_radio_usb_i2c_sensors -t upload
# o, por BLE:
pio run -e Xiao_nrf52_companion_radio_ble_i2c_sensors -t upload
```

Con `MESH_DEBUG` habilitado (comentado por defecto en ambos envs), el
log de arranque muestra qué direcciones respondieron el escaneo y qué
sensores se inicializaron correctamente — útil para confirmar el
cableado sin depender de una petición de telemetría completa.

## Ver también

- `README_I2C.md` — actuador I2C sobre PCF8574 (comando de canal, no
  telemetría formal).
- `src/helpers/sensors/EnvironmentSensorManager.cpp` — tabla completa de
  sensores soportados (`SENSOR_TABLE[]`) y su lógica de detección.
