# Flashear el firmware del catcher TRACE

Firmware: `examples/trace_catcher` de este repo, env de PlatformIO **`Xiao_nrf52_trace_catcher`**.
Placa: **Seeed XIAO nRF52840 + Wio-SX1262** (radio LoRa) con antena de 915 MHz conectada.

> ⚠️ Flashear **reemplaza** el firmware que tenga el XIAO (repetidor, companion, etc.). Para volver a ellos, flashea su env correspondiente (ver «Volver a otro firmware»).

## Requisitos

| Qué | Cómo |
|---|---|
| PlatformIO Core ≥ 6.1 | `pip install platformio` (o la extensión de VS Code) |
| Este repo en la rama `feature/i2c-actuator-public-channel` | el firmware usa el core de este mismo repo; no hay submódulos |
| Cable USB **de datos** | un cable solo de carga no crea el puerto serie |
| Antena conectada a la Wio-SX1262 | el catcher transmite a 22 dBm por defecto: **nunca** transmitas sin antena |

La primera compilación descarga la plataforma `nordicnrf52` y el framework Adafruit (el fork de MeshCore): necesita red y tarda unos minutos.

## 1. Compilar y flashear

```bash
cd ping-ponger-meshcore/firmware
pio run -e Xiao_nrf52_trace_catcher -t upload
```

PlatformIO reinicia el XIAO en modo bootloader (abre y cierra el puerto a 1200 baudios), sube el paquete `firmware.zip` por DFU y lo reinicia. Termina con:

```
Device programmed.
========================= [SUCCESS] ...
```

Solo compilar, sin flashear: `pio run -e Xiao_nrf52_trace_catcher`. El paquete queda en `.pio/build/Xiao_nrf52_trace_catcher/firmware.zip`.

Tamaño actual: ≈ 256 KB de 811 KB de flash y ≈ 19 KB de 235 KB de RAM.

### Variante Faketec (ProMicro nRF52840 + SX1262)

Mismo firmware, env **`ProMicro_trace_catcher`** (en `variants/promicro/platformio.ini`):

```bash
pio run -e ProMicro_trace_catcher -t upload
```

Los comandos, el protocolo y el BLE son idénticos. Diferencias:

- Para entrar al bootloader a mano, haz doble reset puenteando `RST`–`GND` dos veces rápido.
- No trae `NRF52_POWER_MANAGEMENT`, porque `PromicroBoard` no implementa el apagado por bajo voltaje. Por eso no hay protección de arranque con batería baja.
- El paquete queda en `.pio/build/ProMicro_trace_catcher/firmware.zip`.

### LilyGo T-Beam: excluida

No hay env de T-Beam. Su SX1276 no transmite por debajo de 2 dBm por la salida PA_BOOST (la única cableada a la antena), y el experimento con el ponger necesita bajar más. Detalle en `CLAUDE.md` de la raíz del monorepo.

### Si no encuentra el dispositivo o falla la subida

1. **Doble reset:** pulsa dos veces seguidas, rápido, el botón de reset del XIAO. El LED debe pulsar despacio (bootloader). Repite el comando de `upload`.
2. **Puerto:** comprueba que aparece uno: `ls /dev/cu.usbmodem*` (macOS) o `ls /dev/ttyACM*` (Linux). Si no aparece, cambia de cable o de puerto USB.
3. **Varios dispositivos serie conectados:** indica el puerto: `pio run -e Xiao_nrf52_trace_catcher -t upload --upload-port /dev/cu.usbmodemXXXX`.
4. **Permisos (Linux):** añade tu usuario al grupo `dialout` y vuelve a iniciar sesión.

## 2. Verificar que arrancó

### Por USB serie (115200 8N1)

```bash
pio device monitor -b 115200
```

Escribe `HELLO` y Enter. Debe responder:

```
#HELLO fw=0.2.0 proto=2 role=catcher radio=SX1262 id=XXXXXX
```

Y con `CFG?`, la configuración por defecto (preset Australia de MeshCore, 22 dBm, sin hash):

```
#CFG freq=915.800 bw=250 sf=10 cr=5 txp=22 hash= rate=1500 timeout=auto raw=0
```

Si no responde: espera 3 s tras conectar (el firmware espera a que el puerto USB esté listo) y reintenta; si sigue mudo, repite el flasheo.

### Por Bluetooth

El catcher anuncia el servicio Nordic UART con el nombre **`PingPong-CA-XXXXXXXX`** (8 hex del DEVICEID del nRF52; los 6 finales son el `id` del `#HELLO`).

Desde una Mac o Linux con Python:

```bash
pip install bleak
python3 examples/trace_catcher/tools/ble_smoke.py --hash 4F --pings 5
```

Salida esperada: dispositivo encontrado, conexión en < 1 s, `HELLO`/`CFG?` correctos y `N/N ecos` con la latencia comando→`P,` (≈ 1.4 s con el preset por defecto). Sin `--hash` usa `4F`: pon el primer byte de la clave pública de **tu** repetidor.

También sirve **nRF Connect** (iOS/Android): conecta a `PingPong-CA-XXXXXXXX`, activa las notificaciones de la característica TX (`6E400003-…`) y escribe en RX (`6E400002-…`) `HELLO` + salto de línea.

Solo admite **una** conexión BLE a la vez y deja de anunciarse mientras hay una.

## 3. Primer uso

El catcher necesita saber a qué repetidor medir: el inicio de su clave pública.

- **Ponger** (sección [Ponger](#ponger-repetidor-de-laboratorio)): los **8 primeros hex** (4 bytes), por ejemplo `SET hash=4F1A2B3C`. Con 4 bytes no responde por error ningún repetidor real cercano.
- **Repetidor MeshCore stock**: el **primer byte** (2 hex, los dos primeros dígitos de la *public key* en la app MeshCore). Es el modo antiguo: los comandos `PONG` no funcionan con él.

```
SET hash=4F
PING            →  P,1,5E2A9C01,ok,388,+9.50,+8.25,-33.0,1
PING 5          →  5 líneas P, y una S, con las medianas
SAVE            →  #OK saved      (recuerda la configuración tras reiniciar)
```

Lectura de `P,<seq>,<tag>,ok,<rtt>,<snr_subida>,<snr_bajada>,<rssi_bajada>,<n_resp>`: el SNR de subida es el del repetidor oyendo al catcher; el de bajada, el del catcher oyendo al repetidor. Todos los comandos y campos: `specs/001-trace-catcher-firmware/contracts/line-protocol.md` (en el repo MeshAzimuth) y el README de esta carpeta.

Con la app **MeshAzimuth**: edita el repetidor, escribe su prefijo hash y, en la pantalla de apuntamiento, toca «Conectar catcher».

## Seguridad y buen uso de la mesh

- Transmite **solo** tramas `TRACE`/`DIRECT` (12 bytes con hash de 1 byte, 15 con hash de 4) y tramas LAB (`RAW_CUSTOM`/`DIRECT` de 0 saltos, que ningún nodo reenvía). Nunca inunda la mesh.
- El propio firmware impone `rate ≥ 1000 ms` entre inicios de TX y un ciclo de trabajo ≤ 10 % en 60 s (con el preset por defecto, ≈ 33 TX por minuto).
- Potencia por defecto **22 dBm**, con techo de compilación `MAX_TX_DBM=22` (`SET txp=<−9…22>` para bajarla; en laboratorio, `SET txp=2`). El cumplimiento de la normativa de PIRE de tu país y de tu antena es responsabilidad tuya.
- BLE v1 no pide emparejamiento: cualquiera dentro del alcance puede conectarse y enviar comandos mientras no haya otro central conectado.

## Cambiar la configuración de radio

`SET freq=…`, `bw`, `sf`, `cr`, `txp` (se aplican en caliente) y `SAVE` para persistirlos. Debe coincidir con la de la mesh y la del repetidor (por defecto 915.8 MHz, BW 250, SF10, CR 5). Si la configuración guardada se corrompe, el catcher arranca con los valores por defecto y emite `#WARN cfg_reset`.

## Volver a otro firmware

Flashea el env correspondiente del mismo repo, por ejemplo el companion BLE:

```bash
pio run -e Xiao_nrf52_companion_radio_ble -t upload
```

El catcher se puede reflashear en cualquier momento con el comando de la sección 1.

## Problemas frecuentes

| Síntoma | Causa probable |
|---|---|
| Siempre `timeout` en `PING` | hash equivocado, repetidor fuera de alcance, antena desconectada, o preset de radio distinto al de la mesh |
| `timeout` aunque el repetidor se oye en `MON on` | el repetidor tiene `direct.txdelay` mayor al estándar: fija `SET timeout=<ms>` a mano |
| `!ERR state nohash` | falta `SET hash=XX` |
| `!ERR state nohash4` | `PONG …` exige el hash de 8 hex del ponger |
| `G,<seq>,timeout` | hash o `labkey` distintos de los del ponger, o radio distinta: `PONG GET` tras revisar `SET hash` y `SET labkey` |
| `!ERR state busy` | ya hay un `PING` en curso: espera su `P,` (≈ 1.4 s) |
| `#WARN duty_wait` | el límite de TX del catcher retrasó un envío: usa intervalos ≥ 2 s |
| No aparece `PingPong-CA-…` por BLE | otro central ya está conectado (solo se admite uno), o Bluetooth del teléfono sin permiso |
| La app dice «Bluetooth apagado / sin permiso» | actívalo en Ajustes → MeshAzimuth |

---

## Ponger (repetidor de laboratorio)

El ponger es la **estación de control** del ensayo: un `simple_repeater` compilado con `-D PONGER`. Lleva tu mejor antena y no se toca en toda la sesión. Las antenas se cambian en el catcher. Procedimiento completo: `CLAUDE.md` del monorepo, §6.

Qué hace distinto de un repetidor:
- Solo atiende **TRACE DIRECT** y tramas **LAB**. Descarta floods, adverts, logins y DIRECT de la malla real: no la toca aunque comparta canal.
- **No guarda nada**: cada arranque aplica los valores de laboratorio, aunque hubiera prefs guardados en flash. Reiniciarlo = volver a los valores por defecto.
- Se configura **por LoRa desde el catcher** (comandos `PONG …`). Su BLE es de solo lectura.

| Valor por defecto | |
|---|---|
| Radio | 915.8 MHz, BW 250, SF10, CR 5 (preset Australia) |
| TX | **2 dBm** (techo `MAX_TX_DBM=22`) |
| Adverts | desactivados |
| Nombre | `Ponger` |
| `labkey` | `password`, fija en el firmware e independiente de la contraseña de admin (para otra, compila con `-D PONGER_LABKEY='"otra"'` y en el catcher `SET labkey=otra`) |

### Flashear

```bash
cd ping-ponger-meshcore/firmware
pio run -e Xiao_nrf52_ponger -t upload    # XIAO: doble reset si no entra al bootloader
pio run -e ProMicro_ponger -t upload      # Faketec: puentear RST–GND dos veces
```

Solo por **USB**: el OTA por BLE del CLI (`start ota`) no funciona en el ponger, porque su BLE ya está en uso.

### Su hash

Lo necesita el catcher (`SET hash=`). Lo tienes de tres formas:
- En el nombre BLE: `PingPong-PO-4F1A2B3C` ⇒ `4F1A2B3C`.
- Por USB al arrancar: `Repeater ID: 4F1A2B3C…`. Son los 8 primeros hex.
- Con el CLI por USB: `get public.key`.

### Primer uso desde el catcher

```
SET hash=4F1A2B3C
SET txp=2                 tx_max simétrico con el ponger
PONG GET                  →  G,1,ok,915.800,250,10,5,2,22,-41,+9.50
PING 5
PONG TX=-5                →  G,2,ok,…,-5,22,…      (el ponger baja a −5 dBm; manda también SET txp=-5)
PONG RADIO freq=916.800 bw=250 sf=10 cr=5   →  G,…,ok,916.800,…   (cambian los dos)
PONG RESET                vuelve a los valores por defecto
```

`G,<seq>,ok|range|badop,<freq>,<bw>,<sf>,<cr>,<tx>,<tx_ceiling>,<rssi_up>,<snr_up>`: `rssi_up` es el RSSI con que el ponger oyó el comando. Si la `labkey` del ponger no es `password`, manda antes `SET labkey=<clave>` al catcher (no se guarda: hay que repetirlo tras reiniciar).

Después de `PONG RADIO`, si el ponger no oye en **30 min** ninguna trama LAB ni un TRACE suyo, vuelve solo a la radio por defecto. La `tx` no se revierte.

### BLE (solo lectura)

Conecta con nRF Connect a `PingPong-PO-…` (mismo servicio NUS que el catcher):

```
HELLO   →  #HELLO fw=v1.18.0 proto=2 role=ponger radio=SX1262 id=4F1A2B3C
CFG?    →  #CFG freq=915.800 bw=250 sf=10 cr=5 tx=2 tx_ceiling=22
T,<ms>,<rssi>,<snr>,<tag>        cada TRACE dirigido al ponger: el rssi_up en vivo
L,<seq>,ok,<freq>,<bw>,<sf>,<cr>,<tx>   cada comando LAB que responde
```
Cualquier otro comando responde `!ERR readonly`.

### Volver a repetidor normal

Flashea `Xiao_nrf52_repeater` o `ProMicro_repeater`. Ojo: el repetidor normal sí usa los prefs guardados en flash, que pueden ser los de antes del ponger o los de laboratorio (si usaste un comando del CLI que guarda, como `password`). Revisa radio y `tx` con `get radio` y `get tx`.
