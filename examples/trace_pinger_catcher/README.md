# Pinger + catcher en una sola tarjeta

XIAO nRF52840 + Wio-SX1262 o Faketec (ProMicro nRF52840 + SX1262). Un solo firmware, dos modos que la app cambia con `MODE`. **Arranca siempre en `catcher`** (no transmite); el modo no se guarda y la configuración tampoco: la app fija hash, frecuencia y modo en cada conexión.

| Modo | Hace | Comandos |
|---|---|---|
| `catcher` | solo escucha y mide dBm (RSSI instantáneo, 0,5 dB) | `HELLO`, `CFG?`, `SET freq\|bw\|sf\|cr`, `STOP`, `MEAS [n]`, `ID`, `MODE` |
| `pinger` | TRACE DIRECT a un repetidor (12 o 15 bytes, nada más sale por la antena) | `HELLO`, `CFG?`, `SET`, `PING [n]`, `GO`, `STOP`, `ID`, `MODE` |

Lo que no es del modo activo responde `!ERR mode`; lo que ya no existe (`SAVE`, `MON`, `RF`, `PONG`, `LAST?`, `wait=`, `MEAS?`), `!ERR unknown`. `MODE catcher|pinger` responde `#OK mode=…`; con un ping en vuelo, `!ERR state busy` (hacer `STOP` antes).

`#HELLO fw=0.8.0 proto=3 role=<modo> radio=SX1262 id=<6 hex>`. Nombre BLE: `MeshAz-<8 hex>` (solo este firmware lo usa, las placas anteriores no aparecen en la app).

`MEAS [n]` → `M,<n>,<kept>,<mean>,<sd>,<mediana>,<min>,<max>` (n = 16..256, por defecto 64).

## Compilar y flashear

```bash
pio run -e Xiao_nrf52_trace_pinger_catcher -t upload      # o ProMicro_trace_pinger_catcher
```

Doble reset rápido si no entra en el bootloader; cable USB de datos.

## Testbed BLE (hardware real)

`tools/ble_testbed.py` valida la placa flasheada por BLE, como lo haría la app y con una sola conexión: escaneo `MeshAz-*` (sin placas legacy), `HELLO` proto 3, `MODE` en ambos sentidos con el gating (`PING` solo en pinger, `MEAS` solo en catcher), reset de radio al cambiar de modo, `MEAS` contra una baliza CW real, `PING` y los comandos eliminados (`PONG`, `RF`, `LAST?`, `MON`, `SAVE` → `!ERR unknown`).

```bash
pip install bleak
python3 examples/trace_pinger_catcher/tools/ble_testbed.py --freq 914.000 --min-dbm -70
```

- Necesita una baliza CW en `--freq` a poca distancia (a 1 m se miden ≈ -53 dBm, sd < 1 dB) y permiso de Bluetooth para la terminal.
- No necesita otra placa: el `PING` solo comprueba que transmite y acaba en `timeout`. Un ping con respuesta requiere un segundo nodo que conteste al TRACE.
- Con varias placas, `--name MeshAz-XXXXXXXX`. Sale con código 1 si falla alguna comprobación.
- Es independiente del estado previo de la placa (hash ya fijado, modo anterior); la comprobación `nohash` solo se hace si la placa arrancó sin hash.

## Tests (host)

```bash
./examples/trace_pinger_catcher/run_tests.sh
```
