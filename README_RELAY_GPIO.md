# Actuador de relay de 4 canales por GPIO directo (LilyGo T3S3 SX1276)

Esta variante del firmware companion de MeshCore añade una funcionalidad
adicional: cuando el nodo recibe, en un canal de grupo, un mensaje de texto
que coincide con un patrón de comando configurable (`PIN<n>_ON` /
`PIN<n>_OFF`), activa o desactiva el canal `<n>` (0-3) de un relay de 4
canales conectado directamente a 4 pines GPIO de la placa — sin chip
expansor I2C intermedio.

- **Target de hardware**: LilyGo T3S3 con módulo LoRa SX1276 (entorno
  PlatformIO `LilyGo_T3S3_sx1276_companion_radio_ble`).
- **Punto de enganche en el código**: `MyMesh::onChannelMessageRecv()` en
  `examples/companion_radio/MyMesh.cpp`.
- **Driver del actuador**: `src/helpers/actuators/GPIORelayActuator.h/.cpp`
  (lógica pura y testeada nativamente en
  `src/helpers/actuators/GPIORelayLogic.h/.cpp`).

## Cómo funciona

1. Cualquier persona que conozca la clave del canal envía un mensaje de
   texto al canal (ej. `PIN2_ON` para activar el canal 2). El firmware
   compara el comando como **sufijo** del mensaje, porque el texto real que
   viaja por la malla siempre lleva el prefijo `"<nombre_nodo_emisor>: "`.
2. El firmware valida que el canal por el que llegó sea un **canal hashtag
   autorizado** (secreto = `sha256(nombre_del_canal)[:16]`) antes de actuar
   — esto excluye automáticamente el canal `Public` y cualquier canal
   privado de clave aleatoria.
3. Si pasa la validación, el firmware escribe directamente sobre el pin GPIO
   correspondiente (`digitalWrite`), traduciendo el estado lógico ON/OFF al
   nivel eléctrico correcto según la polaridad configurada.
4. `PIN_STATUS` (siempre disponible, sin flag) responde con el estado
   lógico de los 4 canales: `STATE=b0000`..`STATE=b1111` (posición =
   índice de canal, izquierda a derecha).
5. Por defecto no hay respuesta por la malla. Con el flag opcional
   `ACTUATOR_SEND_ACK`, cada escritura exitosa genera un ack:
   `PIN2=ON STATE=b0100`.

## Pines y polaridad

| Canal | GPIO | Build flag |
|---|---|---|
| 0 | 16 | `RELAY_PIN0` |
| 1 | 15 | `RELAY_PIN1` |
| 2 | 39 | `RELAY_PIN2` |
| 3 | 40 | `RELAY_PIN3` |

`RELAY_ACTIVE_LOW=1` está definido por defecto: GPIO en LOW energiza el
relé (módulos típicos basados en `SRD-05VDC-SL-C`). Si tu módulo es
active-high, quita ese flag antes de operar con cargas reales.

**Verificación física obligatoria antes de energizar cargas**: estos 4
pines fueron elegidos por eliminación contra los build flags ya usados por
este board (LoRa, botón, I2C del display/RTC) y contra el pinout público del
T3S3 — no contra el header físico real de tu unidad. Confirma con
multímetro que cada GPIO llega al conector que vas a usar, y confirma la
polaridad de tu módulo de relay concreto antes de conectar cargas.

GPIO18, aunque a veces se lista como "libre" en documentación genérica del
ESP32-S3, está reservado en este proyecto como `PIN_BOARD_SDA` (bus I2C
compartido por el display OLED y el RTC) — no lo reutilices para el relay
en este env.

## Configurar el canal y el patrón de comando

Igual que el resto de actuadores de MeshCore por comando de canal: crea un
canal cuyo nombre empiece con `#` desde la app companion (ver
`docs/companion_protocol.md`), y opcionalmente ajusta el prefijo/sufijos en
`variants/lilygo_t3s3_sx1276/platformio.ini`:

```ini
-D ACTUATOR_CMD_PREFIX='"PIN"'
-D ACTUATOR_CMD_ON_SUFFIX='"_ON"'
-D ACTUATOR_CMD_OFF_SUFFIX='"_OFF"'
-D ACTUATOR_CMD_STATUS='"PIN_STATUS"'
-D ACTUATOR_SEND_ACK=1
```

## Compilar el firmware (.bin) con `build.sh`

El repo trae un script (`build.sh`, en la raíz) que compila el env de
PlatformIO, genera el `.bin` para ESP32 y lo copia a `out/`. `out/` es
**relativo al directorio desde donde ejecutas el script** — si este código
vive en un git worktree (ej.
`.worktrees/feature/t3s3-relay-gpio-actuator/`), el `.bin` aparece en el
`out/` *de ese worktree*, no en el `out/` del checkout principal del repo.
Confirma dónde estás antes de compilar:

```bash
cd .worktrees/feature/t3s3-relay-gpio-actuator   # o la ruta donde vive esta rama
pwd                                               # confírmalo
```

Requiere la variable `FIRMWARE_VERSION` exportada — el script aborta si no
está definida:

```bash
export FIRMWARE_VERSION=v1.0.0
sh build.sh build-firmware LilyGo_T3S3_sx1276_companion_radio_ble
```

Esto produce, en `out/` (dentro del directorio desde el que corriste el
comando):

- `LilyGo_T3S3_sx1276_companion_radio_ble-v1.0.0-<sha>.bin` — firmware para
  flashear sobre un bootloader/partición ya existente.
- `LilyGo_T3S3_sx1276_companion_radio_ble-v1.0.0-<sha>-merged.bin` —
  imagen completa (bootloader + partición + firmware) para una instalación
  desde cero con `esptool` (`--flash_mode`/offset `0x0`).

`<sha>` es el hash corto del commit actual (`git rev-parse --short HEAD`) —
lo añade el script automáticamente al nombre del archivo. Nota que
`build.sh` hace `rm -rf out` antes de compilar (línea ~267) — cada corrida
limpia el `out/` de ese directorio por completo, incluyendo `.bin` de otras
placas que hayas generado ahí antes.

Variables de entorno opcionales:

```bash
export DISABLE_DEBUG=1   # quita MESH_DEBUG y demás flags de logging antes de compilar
```

Si quieres el `.bin` sin pasar por `build.sh` (sin nombre versionado, solo
para probar rápido), el flujo equivalente en PlatformIO puro es:

```bash
pio run -e LilyGo_T3S3_sx1276_companion_radio_ble
pio run -t mergebin -e LilyGo_T3S3_sx1276_companion_radio_ble
# .pio/build/LilyGo_T3S3_sx1276_companion_radio_ble/firmware.bin
# .pio/build/LilyGo_T3S3_sx1276_companion_radio_ble/firmware-merged.bin
```

## Ver también

- `docs/superpowers/specs/2026-08-31-t3s3-gpio-relay-actuator-design.md` —
  spec de diseño completa, incluyendo las decisiones sobre pines y
  polaridad.
