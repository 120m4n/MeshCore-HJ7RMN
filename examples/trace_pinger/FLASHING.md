# Flashear el firmware del pinger TRACE

Firmware: `examples/trace_pinger` de este repo, env de PlatformIO **`Xiao_nrf52_trace_pinger`**.
Placa: **Seeed XIAO nRF52840 + Wio-SX1262** (radio LoRa) con antena de 915 MHz conectada.

> ⚠️ Flashear **reemplaza** el firmware que tenga el XIAO (repetidor, companion, etc.). Para volver a ellos, flashea su env correspondiente (ver «Volver a otro firmware»).

## Requisitos

| Qué | Cómo |
|---|---|
| PlatformIO Core ≥ 6.1 | `pip install platformio` (o la extensión de VS Code) |
| Este repo en la rama `feature/i2c-actuator-public-channel` | el firmware usa el core de este mismo repo; no hay submódulos |
| Cable USB **de datos** | un cable solo de carga no crea el puerto serie |
| Antena conectada a la Wio-SX1262 | el pinger transmite a 22 dBm por defecto: **nunca** transmitas sin antena |

La primera compilación descarga la plataforma `nordicnrf52` y el framework Adafruit (el fork de MeshCore): necesita red y tarda unos minutos.

## 1. Compilar y flashear

```bash
cd /ruta/a/Meshcore
pio run -e Xiao_nrf52_trace_pinger -t upload
```

PlatformIO reinicia el XIAO en modo bootloader (abre y cierra el puerto a 1200 baudios), sube el paquete `firmware.zip` por DFU y lo reinicia. Termina con:

```
Device programmed.
========================= [SUCCESS] ...
```

Solo compilar, sin flashear: `pio run -e Xiao_nrf52_trace_pinger`. El paquete queda en `.pio/build/Xiao_nrf52_trace_pinger/firmware.zip`.

Tamaño actual: ≈ 256 KB de 811 KB de flash y ≈ 19 KB de 235 KB de RAM.

### Si no encuentra el dispositivo o falla la subida

1. **Doble reset:** pulsa dos veces seguidas, rápido, el botón de reset del XIAO. El LED debe pulsar despacio (bootloader). Repite el comando de `upload`.
2. **Puerto:** comprueba que aparece uno: `ls /dev/cu.usbmodem*` (macOS) o `ls /dev/ttyACM*` (Linux). Si no aparece, cambia de cable o de puerto USB.
3. **Varios dispositivos serie conectados:** indica el puerto: `pio run -e Xiao_nrf52_trace_pinger -t upload --upload-port /dev/cu.usbmodemXXXX`.
4. **Permisos (Linux):** añade tu usuario al grupo `dialout` y vuelve a iniciar sesión.

## 2. Verificar que arrancó

### Por USB serie (115200 8N1)

```bash
pio device monitor -b 115200
```

Escribe `HELLO` y Enter. Debe responder:

```
#HELLO fw=0.1.0 proto=1 radio=SX1262 id=XXXXXX
```

Y con `CFG?`, la configuración por defecto (preset Australia de MeshCore, 22 dBm, sin hash):

```
#CFG freq=915.800 bw=250 sf=10 cr=5 txp=22 hash= rate=1500 timeout=auto raw=0
```

Si no responde: espera 3 s tras conectar (el firmware espera a que el puerto USB esté listo) y reintenta; si sigue mudo, repite el flasheo.

### Por Bluetooth

El pinger anuncia el servicio Nordic UART con el nombre **`YagiPinger-XXXX`** (los 4 hex finales de su dirección BLE).

Desde una Mac o Linux con Python:

```bash
pip install bleak
python3 examples/trace_pinger/tools/ble_smoke.py --hash 4F --pings 5
```

Salida esperada: dispositivo encontrado, conexión en < 1 s, `HELLO`/`CFG?` correctos y `N/N ecos` con la latencia comando→`P,` (≈ 1.4 s con el preset por defecto). Sin `--hash` usa `4F`: pon el primer byte de la clave pública de **tu** repetidor.

También sirve **nRF Connect** (iOS/Android): conecta a `YagiPinger-XXXX`, activa las notificaciones de la característica TX (`6E400003-…`) y escribe en RX (`6E400002-…`) `HELLO` + salto de línea.

Solo admite **una** conexión BLE a la vez y deja de anunciarse mientras hay una.

## 3. Primer uso

El pinger necesita saber a qué repetidor medir: el **primer byte de su clave pública** (2 hex; aparece en la app MeshCore, en los dos primeros dígitos de la *public key* del repetidor).

```
SET hash=4F
PING            →  P,1,5E2A9C01,ok,388,+9.50,+8.25,-33.0,1
PING 5          →  5 líneas P, y una S, con las medianas
SAVE            →  #OK saved      (recuerda la configuración tras reiniciar)
```

Lectura de `P,<seq>,<tag>,ok,<rtt>,<snr_subida>,<snr_bajada>,<rssi_bajada>,<n_resp>`: el SNR de subida es el del repetidor oyendo al pinger; el de bajada, el del pinger oyendo al repetidor. Todos los comandos y campos: `specs/001-trace-pinger-firmware/contracts/line-protocol.md` (en el repo MeshAzimuth) y el README de esta carpeta.

Con la app **MeshAzimuth**: edita el repetidor, escribe su prefijo hash y, en la pantalla de apuntamiento, toca «Conectar pinger».

## Seguridad y buen uso de la mesh

- Transmite **solo** tramas `TRACE`/`DIRECT` de 12 bytes, nunca inunda la mesh.
- El propio firmware impone `rate ≥ 1000 ms` entre inicios de TX y un ciclo de trabajo ≤ 10 % en 60 s (con el preset por defecto, ≈ 33 TX por minuto).
- Potencia por defecto **22 dBm** sin tope por firmware (`SET txp=<−9…22>` para bajarla). El cumplimiento de la normativa de PIRE de tu país y de tu antena es responsabilidad tuya.
- BLE v1 no pide emparejamiento: cualquiera dentro del alcance puede conectarse y enviar comandos mientras no haya otro central conectado.

## Cambiar la configuración de radio

`SET freq=…`, `bw`, `sf`, `cr`, `txp` (se aplican en caliente) y `SAVE` para persistirlos. Debe coincidir con la de la mesh y la del repetidor (por defecto 915.8 MHz, BW 250, SF10, CR 5). Si la configuración guardada se corrompe, el pinger arranca con los valores por defecto y emite `#WARN cfg_reset`.

## Volver a otro firmware

Flashea el env correspondiente del mismo repo, por ejemplo el companion BLE:

```bash
pio run -e Xiao_nrf52_companion_radio_ble -t upload
```

El pinger se puede reflashear en cualquier momento con el comando de la sección 1.

## Problemas frecuentes

| Síntoma | Causa probable |
|---|---|
| Siempre `timeout` en `PING` | hash equivocado, repetidor fuera de alcance, antena desconectada, o preset de radio distinto al de la mesh |
| `timeout` aunque el repetidor se oye en `MON on` | el repetidor tiene `direct.txdelay` mayor al estándar: fija `SET timeout=<ms>` a mano |
| `!ERR state nohash` | falta `SET hash=XX` |
| `!ERR state busy` | ya hay un `PING` en curso: espera su `P,` (≈ 1.4 s) |
| `#WARN duty_wait` | el límite de TX del pinger retrasó un envío: usa intervalos ≥ 2 s |
| No aparece `YagiPinger-…` por BLE | otro central ya está conectado (solo se admite uno), o Bluetooth del teléfono sin permiso |
| La app dice «Bluetooth apagado / sin permiso» | actívalo en Ajustes → MeshAzimuth |
