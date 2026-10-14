# Pinger TRACE (XIAO nRF52840 + Wio-SX1262)

Emite una trama `TRACE` DIRECT de un salto hacia un repetidor y mide el SNR de subida (del eco), el SNR/RSSI de bajada y el RTT. Spec: `MeshAzimuth/specs/001-trace-pinger-firmware/`.

## Compilar y flashear

```bash
pio run -e Xiao_nrf52_trace_pinger -t upload      # doble reset del XIAO si pide el bootloader UF2
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

## Pruebas de la lógica (host, sin hardware)

```bash
./examples/trace_pinger/run_tests.sh
```

`tracecore/` no depende de Arduino; `main.cpp` es solo el cascarón de radio, USB y flash.
