# Deploy: firmware companion con actuador I2C (XIAO nRF52840 + Wio-SX1262)

Instrucciones para compilar y flashear el firmware companion (USB o BLE)
con el soporte de actuador I2C descrito en `README_I2C.md`.

Hay dos targets, según cómo quieras hablar con la app companion. La
funcionalidad del actuador es idéntica en ambos:

| Target                              | Transporte companion |
|--------------------------------------|-----------------------|
| `Xiao_nrf52_companion_radio_usb`      | USB (serie)           |
| `Xiao_nrf52_companion_radio_ble`      | Bluetooth LE          |

Sustituye `<TARGET>` por el que corresponda en los comandos de abajo.

## Requisitos

- [PlatformIO Core](https://docs.platformio.org) instalado (`pio --version`).
- Un XIAO nRF52840 con módulo Wio-SX1262 conectado.
- Cable USB-C (necesario para flashear en ambos casos; BLE solo se usa
  después de flashear, para hablar con la app).

## 1. Compilar

Desde la raíz del repositorio:

```bash
sh build.sh build-firmware <TARGET>
```

Esto ejecuta `pio run -e <TARGET>`, convierte el `.hex` resultante a `.uf2`
y deja los artefactos en `out/`:

```
out/<TARGET>-<version>.uf2
out/<TARGET>-<version>.bin
out/<TARGET>-<version>.zip
```

El `.zip` es un paquete estándar de Secure DFU de Nordic (contiene
`firmware.bin` + `firmware.dat` + `manifest.json`), generado
automáticamente por el toolchain de Adafruit/nrfutil como parte del build
normal — no hace falta armarlo a mano. Es el archivo que se sube a
[meshcore.io/flasher](https://meshcore.io/flasher) (ver
[Opción B](#opción-b-meshcoreioflasher-zip-dfu) más abajo).

Alternativa equivalente, sin pasar por `build.sh` (los artefactos quedan en
`.pio/build/<TARGET>/` en vez de `out/`):

```bash
pio run -e <TARGET>
```

Para compilar sin flags de depuración (build de producción):

```bash
export DISABLE_DEBUG=1
sh build.sh build-firmware <TARGET>
```

## 2. Flashear

Hay dos formas de llevar el firmware compilado a la placa. Cualquiera de
las dos deja el mismo firmware corriendo; usa la que te resulte más cómoda.

### Opción A: UF2 manual (arrastrar y soltar)

El XIAO nRF52840 usa un bootloader UF2, no requiere herramientas
adicionales de flasheo, sea cual sea el target (el bootloader solo
entiende UF2 por USB; el firmware BLE también se flashea por USB, el BLE
solo se usa después, en tiempo de ejecución):

1. Conecta la XIAO por USB.
2. Entra en modo bootloader haciendo **doble clic rápido** sobre el botón
   de reset de la placa. El LED debe empezar a pulsar y debe aparecer un
   dispositivo de almacenamiento USB nuevo (normalmente llamado `XIAO-SENSE`
   o `NRF52BOOT`).
3. Copia el archivo `.uf2` generado al volumen que apareció:

   ```bash
   cp out/<TARGET>-*.uf2 /Volumes/XIAO-SENSE/
   ```

   (ajusta la ruta de montaje según tu sistema operativo; en Linux suele
   montarse en `/media/$USER/XIAO-SENSE` o similar).
4. La placa se reinicia sola y arranca con el nuevo firmware. El volumen
   USB desaparece automáticamente al terminar de flashear.

No existe un comando CLI de "un solo paso" para flashear (`pio run -t
upload`) para estos targets porque el `upload_protocol` configurado es
`nrfutil`, pensado para flasheo por DFU/BLE, no para el bootloader UF2 de
fábrica de la XIAO.

### Opción B: meshcore.io/flasher (.zip DFU)

1. Conecta la XIAO por USB (mismo cable, no hace falta entrar manualmente
   en modo bootloader UF2; el flasher web maneja el protocolo Secure DFU
   directamente).
2. Abre [meshcore.io/flasher](https://meshcore.io/flasher).
3. Cuando pida un firmware personalizado / custom firmware, sube el
   archivo `out/<TARGET>-<version>.zip` generado en el paso 1.
4. Sigue las instrucciones en pantalla del flasher para completar la
   subida.

No pude verificar desde este entorno los detalles exactos de la interfaz
de meshcore.io/flasher (WebUSB/WebBluetooth, pasos en pantalla, etc.) — lo
único confirmado y probado localmente es que el `.zip` que produce el
build (`firmware.bin` + `firmware.dat` + `manifest.json`, paquete Secure
DFU estándar de Nordic) es el formato que ese flasher pide.

## 3. Verificar

Conecta un cliente companion (app MeshCore, `meshcore.js`, `meshcore_py`,
etc.) al nodo:

- Si flasheaste `Xiao_nrf52_companion_radio_usb`: por el puerto serie USB.
- Si flasheaste `Xiao_nrf52_companion_radio_ble`: por Bluetooth LE,
  emparejando con el PIN configurado (`BLE_PIN_CODE`, por defecto
  `123456` en `variants/xiao_nrf52/platformio.ini`).

Luego, en cualquiera de los dos casos:

1. Envía `ACTUATOR_ON` al canal configurado (por defecto, `Public`).
2. Debes ver el LED de la XIAO parpadear brevemente y el pin
   `PCF8574_ACTUATOR_PIN` del PCF8574 cambiar de estado (medible con un
   multímetro o LED de prueba, o usando el sketch de
   `tools/i2c_actuator_nano/` como actuador de prueba).
3. Envía `ACTUATOR_OFF` para revertir el estado.

Ver `README_I2C.md` para el detalle de configuración (canal, palabra clave,
dirección I2C).
