# Pinger TRACE (XIAO nRF52840 + Wio-SX1262)

Emite una trama `TRACE` DIRECT de un salto hacia un repetidor y mide el SNR de subida (del eco), el SNR/RSSI de bajada y el RTT. Spec: `MeshAzimuth/specs/001-trace-pinger-firmware/`.

## Compilar y flashear

Guía completa (requisitos, verificación por serie y BLE, problemas frecuentes): [FLASHING.md](FLASHING.md).

```bash
pio run -e Xiao_nrf52_trace_pinger -t upload      # doble reset del XIAO si no entra en el bootloader
pio device monitor -b 115200
```

Preset por defecto (Australia): 915.8 MHz, BW 250, SF10, CR 5, 22 dBm. Sin hash: hay que definirlo.

## Uso (115200 8N1, una línea por comando)

```
HELLO                 #HELLO fw=… proto=1 radio=SX1262 id=…
SET hash=A1           primer byte de la clave pública del repetidor (cámbialo cuando quieras)
PING                  1 ping  ->  P,<seq>,<tag>,ok|timeout|busy|txerr,<rtt>,<snr_up>,<snr_dn>,<rssi_dn>,<n_resp>
PING 5                ráfaga  ->  5 × P + S,<n>,<ok>,<loss%>,<med_up>,<med_dn>,<med_rssi>,<busy>
GO / STOP             pings continuos / detener
MON on|off            escucha: R,<ms>,<rssi>,<snr>,<hex>  (nunca transmite)
CFG? / SET k=v / SAVE freq bw sf cr txp hash rate timeout raw (SAVE persiste en flash)
```

Reglas: `rate ≥ 1000 ms` (entre inicios de TX), ciclo de TX ≤ 10 % en 60 s, solo `TRACE`/`DIRECT`. Contrato completo: `specs/001-trace-pinger-firmware/contracts/line-protocol.md`.

## Tests y compatibilidad

Todo corre en el host, sin hardware ni PlatformIO ni gtest instalado (usa un gtest mínimo en `tests/shim`):

```bash
./examples/trace_pinger/run_tests.sh                       # todas las suites (~4.5 s)
./examples/trace_pinger/run_tests.sh test_interop_packet   # solo algunas
python3 examples/trace_pinger/tools/check_upstream_trace.py   # guardián de deriva (0 = sin deriva)
```

`run_tests.sh` compila **una vez** el `src/Packet.cpp` y `src/Utils.cpp` reales de este repo con `test/mocks`, junto con `tracecore/` y `tests/support/`, y enlaza cada suite. Hay que ejecutarlo dentro del repo Meshcore. No modifica nada fuera de `examples/trace_pinger/`.

| Suite | Qué demuestra |
|---|---|
| `test_trace_frame`, `test_airtime`, `test_config`, `test_stats`, `test_duty_limiter`, `test_line_codec`, `test_pinger_fsm` | La lógica de `tracecore` (fase 001): trama, tiempos, configuración, medianas, límite de TX, protocolo de líneas y máquina de estados. |
| `test_interop_packet` | Lo que arma `buildRequest` lo acepta el `Packet::readFrom` upstream (10 000 casos), `writeTo` → `parseEcho` para todo SNR, identidad de bytes y rechazos. |
| `test_repeater_model` | `RepeaterModel` (`tests/support`) reproduce la decisión TRACE de `Mesh::onRecvPacket`: reenvío, SNR, el eco no se propaga y un tag repetido se descarta. |
| `test_upstream_guard` | El guardián detecta cambios dentro de los fragmentos vigilados y solo esos. |
| `test_field_frames` | Reproduce `tests/fixtures/frames/*.txt` (sintéticos y capturas de campo) contra `tracecore` y upstream. |
| `test_line_fixtures` | `tests/fixtures/lines/*.txt` (P, S, R, comandos, info): el formato C++ reproduce cada línea y cada comando da su respuesta. Son los mismos fixtures que usará el parser Swift. |
| `test_fixtures` | El lector de fixtures y su mini-parser JSON. |

### Guardián de deriva

`tools/check_upstream_trace.py` extrae 10 fragmentos de este core (decisión de reenvío TRACE, `createTrace`, constantes de `Packet.h`/`MeshCore.h`, `isValidPathLen`, `calculatePacketHash`, `writeTo`/`readFrom`, retardo y política de reenvío del repetidor), los normaliza (sin comentarios ni espacios) y compara su SHA-256 con `tests/upstream-trace.lock`. Sale con 0 sin deriva, 1 con deriva (muestra el diff) y 2 si falta un ancla.

Tras traer cambios de upstream: ejecútalo; si falla, lee el diff, actualiza `RepeaterModel` y los contratos si la semántica cambió y luego `python3 tools/check_upstream_trace.py --update`.

### Añadir una captura de campo

1. Con el pinger en `SET raw=1` (ecos) o `MON on` (otros paquetes), copia la línea `R,<t>,<rssi>,<snr>,<HEX>`. Las tramas de más de 64 B llegan truncadas (`..`) y no sirven como fixture.
2. Añade a `tests/fixtures/frames/field-<fecha>.txt` la trama y su expectativa, separadas por un TAB: `<HEX><TAB>{"kind":"echo","tag":"…","hash":"4F","snr_up_q":-25}` (`kind`: `echo`, `request`, `other` o `invalid`; formato completo en `specs/002-trace-frame-host-tests/contracts/fixture-format.md`).
3. `./examples/trace_pinger/run_tests.sh test_field_frames`. Un fixture sin expectativa hace fallar el test con el formato esperado.
4. Evita publicar tráfico de terceros legible: los mensajes del canal Public se descifran con una clave pública.

### Limitaciones conocidas

- `RepeaterModel` duplica la decisión de reenvío de `Mesh.cpp` (no se compila el core entero en host); el guardián es lo que impide que se desincronice.
- El paso del guardián en CI está diferido: requiere añadir un workflow fuera de `examples/trace_pinger/`.
