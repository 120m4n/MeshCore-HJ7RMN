# CLAUDE.md

## Telemetría vectorial `tv` (`src/helpers/tv_telemetry.h`)

El vector se pega en Telegram/WhatsApp, así que su alfabeto no puede llevar
caracteres de formato. Hoy solo emite base64url (`A-Z a-z 0-9 - _`), `.`
(hueco) y `!` (escape).

- `~` está prohibido: se leía como tachado (cambiado a `!` en `ba42310e`).
  Lo protege el test `OutputNeverContainsTildeOrMarkupChars` en
  `test/test_tv_telemetry/`. Los decoders siguen aceptando `~` solo para
  vectores viejos.
- **Riesgo menor abierto:** `_` (de base64url) es cursiva en WhatsApp
  (`_texto_`). Dentro de una palabra (`x_ab_y`) normalmente no se activa, por
  eso se dejó. Si aparecen cursivas en un vector real, reemplazar `_` por
  otro carácter seguro en `B64` exige subir `FORMAT_VERSION` y actualizar
  todos los decoders (py/ts, `tools/tv_edge_ext`, y la copia de
  `meshcore-grid-h3/apps/web/src/lib/tv/`).
