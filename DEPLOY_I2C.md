# Deploy: firmware companion USB con actuador I2C (XIAO nRF52840 + Wio-SX1262)

Instrucciones para compilar y flashear la variante `Xiao_nrf52_companion_radio_usb`
con el soporte de actuador I2C descrito en `README_I2C.md`.

## Requisitos

- [PlatformIO Core](https://docs.platformio.org) instalado (`pio --version`).
- Un XIAO nRF52840 con módulo Wio-SX1262 conectado.
- Cable USB-C.

## 1. Compilar

Desde la raíz del repositorio:

```bash
sh build.sh build-firmware Xiao_nrf52_companion_radio_usb
```

Esto ejecuta `pio run -e Xiao_nrf52_companion_radio_usb`, convierte el
`.hex` resultante a `.uf2` y deja los artefactos en `out/`:

```
out/Xiao_nrf52_companion_radio_usb-<version>.uf2
out/Xiao_nrf52_companion_radio_usb-<version>.bin
```

Alternativa equivalente, sin pasar por `build.sh` (los artefactos quedan en
`.pio/build/Xiao_nrf52_companion_radio_usb/` en vez de `out/`):

```bash
pio run -e Xiao_nrf52_companion_radio_usb
```

Para compilar sin flags de depuración (build de producción):

```bash
export DISABLE_DEBUG=1
sh build.sh build-firmware Xiao_nrf52_companion_radio_usb
```

## 2. Flashear

El XIAO nRF52840 usa un bootloader UF2 (arrastrar y soltar), no requiere
herramientas adicionales de flasheo:

1. Conecta la XIAO por USB.
2. Entra en modo bootloader haciendo **doble clic rápido** sobre el botón
   de reset de la placa. El LED debe empezar a pulsar y debe aparecer un
   dispositivo de almacenamiento USB nuevo (normalmente llamado `XIAO-SENSE`
   o `NRF52BOOT`).
3. Copia el archivo `.uf2` generado al volumen que apareció:

   ```bash
   cp out/Xiao_nrf52_companion_radio_usb-*.uf2 /Volumes/XIAO-SENSE/
   ```

   (ajusta la ruta de montaje según tu sistema operativo; en Linux suele
   montarse en `/media/$USER/XIAO-SENSE` o similar).
4. La placa se reinicia sola y arranca con el nuevo firmware. El volumen
   USB desaparece automáticamente al terminar de flashear.

No existe un comando CLI de "un solo paso" para flashear (`pio run -t
upload`) para este target porque el `upload_protocol` configurado es
`nrfutil`, pensado para flasheo por DFU/BLE, no para el bootloader UF2 de
fábrica de la XIAO; el flujo soportado y recomendado por MeshCore para este
board es el UF2 manual descrito arriba.

## 3. Verificar

Con un cliente companion (app MeshCore, `meshcore.js`, `meshcore_py`, etc.)
conectado por USB al puerto serie que expone la placa:

1. Envía `ACTUATOR_ON` al canal configurado (por defecto, `Public`).
2. Debes ver el LED de la XIAO parpadear brevemente y el pin
   `PCF8574_ACTUATOR_PIN` del PCF8574 cambiar de estado (medible con un
   multímetro o LED de prueba, o usando el sketch de
   `tools/i2c_actuator_nano/` como actuador de prueba).
3. Envía `ACTUATOR_OFF` para revertir el estado.

Ver `README_I2C.md` para el detalle de configuración (canal, palabra clave,
dirección I2C).
