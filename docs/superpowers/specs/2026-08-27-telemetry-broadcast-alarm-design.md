# Telemetría periódica y alarma de umbral por canal (companion_radio)

## Resumen

Nueva feature opt-in para el firmware `companion_radio` de MeshCore: el
nodo transmite proactivamente, a un canal hashtag propio, (a) una lectura
periódica de temperatura/humedad y (b) una alarma cuando la temperatura
cruza un umbral configurable (ej. "se debería usar bloqueador solar").

Motivación: hoy `GetTelemetry` es *pull* (un peer o la app companion
piden el dato). Esta feature agrega un modo *push*, sin exponer nada al
canal Public y sin modificar el actuador ni ningún driver de sensor I2C
nativo.

## Alcance

- **Genérica**, activable en cualquier variante `companion_radio` que ya
  compile `EnvironmentSensorManager` (mismo patrón opt-in que
  `HAS_PCF8574_ACTUATOR`), no atada a un board específico.
- Dos comportamientos **independientes** entre sí:
  - Reporte periódico temp/humedad.
  - Alarma de umbral de temperatura (con recuperación).
- **Restricción dura** (impuesta por el usuario): esta feature no debe
  modificar `src/helpers/sensors/EnvironmentSensorManager.*`, ningún
  driver de sensor I2C nativo, ni `src/helpers/actuators/PCF8574Actuator.*`
  / el código del actuador en `MyMesh.cpp`. Solo puede *consumir* la API
  pública ya existente de esas piezas.

## No-objetivos

- No reemplaza `GetTelemetry` (sigue existiendo, sin cambios).
- No soporta reportar presión/altitud/otros canales CayenneLPP en el
  mensaje periódico — solo temperatura y humedad (ver decisión de
  brainstorming).
- No agrega retransmisión/ack de estos mensajes de canal — mismo modelo
  "fire and forget" que ya usa `sendGroupMessage()` en el resto del
  firmware.
- La histéresis de recuperación de la alarma es una constante de
  compilación, no configurable en runtime.

## Arquitectura

### Hallazgo clave que habilita el diseño

`MyMesh` no sobreescribe `loop()` — `examples/companion_radio/main.cpp`
llama por separado a `the_mesh.loop()`, `sensors.loop()`,
`interface_manager.loop()`, `ui_task.loop()`, etc. en su propio
`loop()`. Y `BaseChatMesh::sendGroupMessage()`, `getChannel()`,
`findChannelIdx()` y `MyMesh::getNodePrefs()` ya son **públicos**. Esto
permite que la nueva feature sea un componente standalone que recibe
`the_mesh`/`sensors`/`prefs` por referencia, en vez de vivir dentro de
`MyMesh`.

### Componentes nuevos

**`examples/companion_radio/TelemetryBroadcaster.h/.cpp`** (archivo
nuevo, sin dependencias de `MyMesh.h`):

```cpp
class TelemetryBroadcaster {
public:
  void loop(BaseChatMesh& mesh, EnvironmentSensorManager& sensors, NodePrefs& prefs);

private:
  bool findConfiguredChannel(BaseChatMesh& mesh, ChannelDetails& out);
  bool readTempHumidity(EnvironmentSensorManager& sensors, float* temp_c, float* hum_pct);
  void sendReading(BaseChatMesh& mesh, const ChannelDetails& ch, float temp_c, float hum_pct);
  void checkAlarm(BaseChatMesh& mesh, const ChannelDetails& ch, float temp_c);

  unsigned long _last_broadcast_ms = 0;
  bool _alarm_active = false;
};
```

- `loop()` no hace nada si `HAS_TELEMETRY_BROADCAST` no está definido
  (todo el `.cpp` queda envuelto en `#ifdef HAS_TELEMETRY_BROADCAST`,
  igual que `AM2301RemoteSensor.cpp` hacía con `HAS_AM2301_SENSOR`).
- Internamente usa su **propio** buffer `CayenneLPP` local (tamaño
  chico, ej. 32 bytes) — no reutiliza el `telemetry` que ya usa
  `MyMesh::onContactRequest()` para `GetTelemetry`, para no acoplarse a
  ese miembro ni arriesgar interferencia si algún día se paraleliza.

**`main.cpp`**: 2-3 líneas nuevas, junto a las demás llamadas `.loop()`
existentes:

```cpp
#ifdef HAS_TELEMETRY_BROADCAST
TelemetryBroadcaster telemetry_broadcaster;
#endif
...
void loop() {
  the_mesh.loop();
  interface_manager.loop();
  sensors.loop();
#ifdef HAS_TELEMETRY_BROADCAST
  telemetry_broadcaster.loop(the_mesh, sensors, *the_mesh.getNodePrefs());
#endif
  ui_task.loop();
  ...
}
```

**`NodePrefs.h`** (`examples/companion_radio/NodePrefs.h`): nuevos
campos + un `TelemetryBroadcastPrefs` calcado de `GPSPrefs` (mismo
archivo que ya declara `gps_interval`, no es código de sensor/actuador):

```cpp
#ifndef TELEMETRY_BROADCAST_INTERVAL_SEC
#define TELEMETRY_BROADCAST_INTERVAL_SEC 1800  // 30 min
#endif
#ifndef TELEMETRY_ALARM_THRESHOLD_C
#define TELEMETRY_ALARM_THRESHOLD_C 45
#endif

uint8_t  telemetry_broadcast_enabled = 1;  // ON by default once HAS_TELEMETRY_BROADCAST is
uint32_t telemetry_broadcast_interval_sec = TELEMETRY_BROADCAST_INTERVAL_SEC;
uint8_t  telemetry_alarm_enabled = 1;      // opted into at build time - same convention as
float    telemetry_alarm_threshold_c = TELEMETRY_ALARM_THRESHOLD_C;  // ACTUATOR_SEND_ACK=1
```

Así el build flag fija el valor por defecto (aplicado una sola vez, al
construir `NodePrefs` con valores de fábrica), pero el campo queda
siendo un dato persistido/editable en runtime vía custom-vars, no una
constante — igual que ya lo requiere el análisis de flujo de datos más
abajo.

```cpp
class TelemetryBroadcastPrefs : public ConfigSerializer {
  NodePrefs* _parent;
protected:
  void structure() override {
    def("bc_en", _parent->telemetry_broadcast_enabled);
    def("bc_int", _parent->telemetry_broadcast_interval_sec);
    def("al_en", _parent->telemetry_alarm_enabled);
    def("al_thr", _parent->telemetry_alarm_threshold_c);
  }
public:
  TelemetryBroadcastPrefs(NodePrefs* parent) : _parent(parent) { }
};
```

Instanciado como miembro `TelemetryBroadcastPrefs telemetry_broadcast;`
(mismo patrón que `GPSPrefs gps;`), inicializado en el constructor de
`NodePrefs` (`telemetry_broadcast(this)`, junto a `gps(this)`), y
agregado a `structure()` de `NodePrefs` como
`def("telem_bc", telemetry_broadcast);`. Persistido/cargado en
`DataStore.cpp` con el mismo mecanismo binario que ya usan
`gps_enabled`/`gps_interval` (ver ese código como referencia exacta al
escribir el plan de implementación).

**`MyMesh.cpp`** — único touchpoint compartido con código existente:
extender el `else if` de `CMD_SET_CUSTOM_VAR` (línea ~1959 hoy, cerca
del bloque `gps_interval`) para reconocer 4 nombres nuevos
(`telemetry_broadcast_enabled`, `telemetry_broadcast_interval_sec`,
`telemetry_alarm_enabled`, `telemetry_alarm_threshold_c`) **antes** de
delegar a `sensors.setSettingValue()`, y extender el loop de
`CMD_GET_CUSTOM_VARS` para incluirlos en la enumeración. Esto expone
las 4 opciones en la misma UI de "custom vars" que ya usa la app
companion para `gps_interval` — sin tocar `EnvironmentSensorManager`.
Ambas ramas son aditivas (un `else if` más), no modifican la lógica de
GPS/actuador existente.

### Flujo de datos

```
main.cpp loop()
  → telemetry_broadcaster.loop(the_mesh, sensors, prefs)
      1. si !prefs.telemetry_broadcast_enabled && !prefs.telemetry_alarm_enabled: return
      2. findConfiguredChannel(the_mesh, ch)  — si no existe el canal aún, return (debug log)
      3. readTempHumidity(sensors, &temp_c, &hum_pct):
           CayenneLPP buf(32); buf.reset();
           sensors.querySensors(0xFF, buf);              // API pública ya existente
           StaticJsonDocument<256> doc;
           JsonArray arr = doc.to<JsonArray>();
           buf.decode(buf.getBuffer(), buf.getSize(), arr);
           // recorrer arr buscando type == LPP_TEMPERATURE (103) / LPP_RELATIVE_HUMIDITY (104)
           // (ArduinoJson + CayenneLPP ya son dependencias transitivas existentes, sin lib_deps nuevo)
      4. si prefs.telemetry_broadcast_enabled && interval_sec > 0
            && pasó interval_sec desde _last_broadcast_ms:
           sendReading(...)  → sendGroupMessage(ch.channel, "TEMP=%.1fC HUM=%.1f%%")
      5. si prefs.telemetry_alarm_enabled:
           checkAlarm(...)   → compara temp_c vs threshold_c con histéresis fija de 2°C
```

### Máquina de estados de la alarma

- `!_alarm_active && temp_c > threshold_c` → dispara alarma, envía
  mensaje, `_alarm_active = true`.
- `_alarm_active && temp_c < (threshold_c - 2.0f)` → envía mensaje de
  recuperación, `_alarm_active = false`.
- Cualquier otro caso → sin acción (evita "parpadeo" cerca del umbral).

### Resolución del canal por nombre

No existe hoy un lookup de canal por nombre (solo `findChannelIdx(GroupChannel)`,
que va en la dirección inversa). `findConfiguredChannel()` es un loop
chico, local a `TelemetryBroadcaster.cpp`:

```cpp
bool TelemetryBroadcaster::findConfiguredChannel(BaseChatMesh& mesh, ChannelDetails& out) {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (mesh.getChannel(i, out) && strcmp(out.name, TELEMETRY_BROADCAST_CHANNEL) == 0) {
      return true;
    }
  }
  return false;
}
```

(El resultado podría cachearse tras el primer hallazgo ya que los
canales no cambian de índice en runtime salvo edición manual — detalle
a decidir en el plan de implementación, no crítico para el diseño.)

## Configuración

### Build flags (`variants/xiao_nrf52/platformio.ini`, u otro board)

```ini
;  -D HAS_TELEMETRY_BROADCAST=1
;  -D TELEMETRY_BROADCAST_CHANNEL='"#miCanal"'
;  -D TELEMETRY_BROADCAST_INTERVAL_SEC=1800
;  -D TELEMETRY_ALARM_THRESHOLD_C=45
```

`TELEMETRY_BROADCAST_CHANNEL` es obligatorio si `HAS_TELEMETRY_BROADCAST`
está activo (falla de compilación clara con `#error` si falta, mismo
estilo defensivo que ya usa el resto del firmware para flags requeridos).
Los otros dos flags son valores default para los campos de `NodePrefs`
(no constantes fijas) — ver siguiente sección.

### Runtime (custom vars, vía companion app o BLE/USB)

- `telemetry_broadcast_enabled` (0/1) — activa/desactiva el reporte
  periódico sin recompilar.
- `telemetry_broadcast_interval_sec` (uint32, segundos) — 0 tiene el
  mismo efecto que `telemetry_broadcast_enabled=0` (desactivado).
- `telemetry_alarm_enabled` (0/1) — activa/desactiva la alarma sin
  recompilar.
- `telemetry_alarm_threshold_c` (float, °C).

## Formato de mensajes

- Periódico: `TEMP=23.5C HUM=45.2%` (mismo estilo que el resto del
  firmware — `PIN_STATUS`, el extinto `TEMP_STATUS`).
- Alarma: `ALERTA: temperatura 47.2C supera 45.0C - se recomienda usar
  bloqueador solar`.
- Recuperación: `Temperatura normalizada: 42.8C`.

## Manejo de errores / casos borde

| Caso | Comportamiento |
|---|---|
| Sensor no detectado / `querySensors` sin canal de temperatura en el CayenneLPP decodificado | Se omite el ciclo, `MESH_DEBUG_PRINTLN`, reintenta en el próximo `loop()` — sin reintento agresivo, no es crítico. |
| `TELEMETRY_BROADCAST_CHANNEL` aún no existe en el nodo (usuario no lo creó todavía) | Se omite con debug log, no crashea; se resuelve solo cuando el canal exista. |
| `CayenneLPP::decode()` retorna error (buffer corrupto/overflow) | Igual que "sin canal de temperatura": se omite el ciclo. |
| Ambos comportamientos desactivados (`broadcast_enabled=0` y `alarm_enabled=0`) | `loop()` retorna en el primer chequeo, sin costo de CPU/I2C extra. |
| `HAS_TELEMETRY_BROADCAST` no definido en el build | Cero código compilado, cero impacto en tamaño de firmware — como el resto de features opt-in de este repo. |

## Testing

- **Test nativo** (`test/test_telemetry_broadcast_alarm/`, sin
  hardware): dos áreas puras, testeables sin Arduino:
  1. Decode de un buffer CayenneLPP sintético (temp+hum conocidos) →
     valores extraídos correctos. Puede probarse contra el mock
     `test/mocks/CayenneLPP.h` existente si cubre `decode()`, o
     extendiendo ese mock (fuera del alcance de "no tocar sensores
     nativos" — es un mock de test, no código de producción).
  2. Máquina de estados de la alarma (`_alarm_active` +
     histéresis): secuencia de temperaturas → secuencia esperada de
     mensajes (ninguno / alarma / nada / recuperación).
- **Build check**: al menos un env con `HAS_TELEMETRY_BROADCAST=1`
  compila limpio, y los envs sin el flag no cambian de tamaño de
  firmware (confirma que el `#ifdef` realmente excluye el código).

## Lo que esto NO toca

- `src/helpers/sensors/EnvironmentSensorManager.h/.cpp` — sin cambios.
- `src/helpers/actuators/PCF8574Actuator.h/.cpp` — sin cambios.
- Cualquier driver de sensor I2C nativo (`Adafruit_BME280`, etc.) — sin
  cambios.
- `examples/companion_radio/MyMesh.h` — sin cambios (el componente
  nuevo no es miembro de `MyMesh`).
- `examples/companion_radio/MyMesh.cpp` — un único bloque aditivo en el
  dispatcher de `CMD_SET_CUSTOM_VAR`/`CMD_GET_CUSTOM_VARS`, sin alterar
  ninguna rama existente (actuador o GPS).

## Ver también

- `README_I2C_TELEMETRY.md` — telemetría formal `GetTelemetry` (pull),
  que este feature complementa sin modificar.
- `README_I2C.md` — modelo de seguridad de canal hashtag que esta
  feature reutiliza (canal propio, nunca Public).
