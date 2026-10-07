# Tips and Tricks

Notas prácticas de operación y diagnóstico. Cada entrada indica de dónde sale la
información (código leído, no necesariamente probado en hardware).

- [Trace funciona, pero el login de admin a un repeater da timeout](#trace-funciona-pero-el-login-de-admin-a-un-repeater-da-timeout)

---

## Trace funciona, pero el login de admin a un repeater da timeout

> **Alcance:** análisis hecho leyendo `src/Mesh.cpp`, `src/helpers/RoutingPolicy.h` y
> `examples/simple_repeater/MyMesh.cpp`. Son hipótesis ordenadas por probabilidad, no
> un diagnóstico confirmado: hace falta el log serial o la topología para afinar.

### Síntoma

Un trace con varios saltos hacia el repeater responde bien, pero al hacer login como
admin desde el cliente se alcanza el timeout.

### Por qué una cosa no prueba la otra

Son dos mecanismos distintos:

| | Trace | Login admin (`ANON_REQ`) |
|---|---|---|
| Ruta | Directa, siguiendo una lista de hashes (`Mesh::onRecvPacket`) | Ida por flood o directa, y **vuelta por otra ruta** |
| Qué exige al repeater | Reenviar si su hash coincide y `allowPacketForward()` acepta | Descifrar y validar MAC, password y timestamp, y que la **respuesta** llegue de vuelta |
| Políticas que lo afectan | Casi ninguna (`disable_fwd`) | Flood, regiones/scope, límites de saltos, detección de loops, airtime |

El trace solo demuestra que la **ida directa** por esos saltos funciona. No dice nada
de la respuesta, del password ni del timestamp. Además, todos los fallos de login son
**silenciosos**: el repeater no responde y desde el cliente todo se ve como timeout.

### Hipótesis, de más a menos probable

1. **La respuesta flood no regresa.**
   - La respuesta va por flood cuando la petición llegó por flood, o cuando el cliente
     es nuevo y no hay `out_path` guardado (`onAnonDataRecv`, `chooseReplyRoute`).
   - Ese flood pasa por `allowPacketForward()` en cada repeater intermedio, que lo
     descarta si: se excede `flood.max` o `flood.max.unscoped`; el scope/región es
     desconocido (`recv_pkt_region == NULL`); o `loop.detect` lo marca como loop.
   - `sendFloodReply()` elige el scope de la respuesta. Una respuesta sin scope muere
     en el primer salto si algún repeater tiene `flood.max.unscoped 0`.
   - El trace es directo y no pasa por nada de esto: explica exactamente el síntoma.

2. **Password o timestamp rechazados en silencio.**
   - Password incorrecto: `handleLoginReq()` devuelve 0 sin responder.
   - Anti-replay: si `sender_timestamp <= last_timestamp` del cliente, también devuelve
     0 (log `Possible login replay attack!`). `last_timestamp` vive en RAM hasta
     reiniciar el repeater.
   - Si un login llegó al repeater pero su respuesta se perdió, un reintento con el
     mismo timestamp puede tomarse por replay (sospecha: no verificado cómo reintenta
     cada cliente).
   - Un cliente con el reloj atrasado respecto a otro que ya hizo login con la misma
     identidad también falla.

3. **Tamaño y airtime.** `ANON_REQ` lleva la clave pública (32 bytes), MAC y password:
   es bastante más grande que un trace, y más aún con muchos saltos. Más airtime
   implica más colisiones y, en repeaters con duty cycle (`airtime factor`), respuestas
   retrasadas más allá del timeout del cliente.

4. **Rate limiter.** Solo aplica a los `ANON_REQ` de regiones, owner y reloj (4 cada
   3 minutos). El login con password no pasa por él.

5. **Cambios propios del fork (TV telemetry).** `tv::sample_tick()` corre en `loop()`;
   si el bus I2C se cuelga podría bloquear el loop, pero eso degradaría también el
   reenvío del trace. Poco probable.

Descartado: crash por tabla de clientes llena. `ClientACL::putClient()` nunca devuelve
`NULL`; expulsa al cliente no admin menos activo.

### Cómo distinguirlas (sin tocar código)

1. **Login con el cliente pegado al repeater (cero saltos).** Si funciona y a varios
   saltos no, apunta a las hipótesis 1 o 3. Si falla también ahí, apunta a password o
   timestamp (2).
2. **Reiniciar el repeater y hacer login enseguida.** Si entra, era el anti-replay.
3. **Revisar los prefs de los repeaters intermedios** con el CLI
   ([`cli_commands.md`](./cli_commands.md)): `get flood.max`, `get flood.max.unscoped`,
   `get loop.detect`, la configuración de regiones y el airtime factor.
4. **Compilar el repeater con `MESH_DEBUG=1` y mirar el serial** (se activa con
   `-D MESH_DEBUG=1` en los `build_flags` del entorno; hay ejemplos, algunos comentados,
   en `variants/xiao_nrf52/platformio.ini`). Cada rama
   imprime su propio mensaje: `Invalid password`, `Possible login replay attack!`,
   `Login success!`, `allowPacketForward: …`. Con eso la causa queda confirmada en un
   solo intento.

### Datos útiles para pedir ayuda

Un intento de login con el log serial del repeater, cuántos saltos hay, qué repeaters
intervienen y sus valores de `flood.max`, `flood.max.unscoped` y `loop.detect`.
