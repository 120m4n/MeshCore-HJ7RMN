# Notas prácticas: repetidor + companion vs. solo companion

Resumen: un companion es un endpoint y **no retransmite**. Un repetidor es la infraestructura que da alcance. Combinarlos separa dos problemas: *dónde va la antena* y *dónde está el usuario*.

Fuentes: `docs/faq.md`, `docs/cli_commands.md`, `src/MeshCore.h`. Las referencias de línea son de la revisión actual.

## 1. Por qué es más factible repetidor + companion

1. **Solo los repetidores reenvían.** Sólo repetidores y room servers con `set repeat on` repiten (`faq.md:478`). Un companion es cliente (`faq.md:129`) y no extiende la malla.
2. **Ubicación de la antena.** El repetidor va alto y con línea de vista. El companion vive con el usuario (bolsillo, escritorio, interior). En LoRa pesan más la altura y la línea de vista que la potencia.
3. **Enlaces separados.** Teléfono↔companion usa BLE, USB o WiFi, que son locales. El tramo difícil de largo alcance lo asume el repetidor.
4. **Recomendación oficial.** Con dos equipos y otros usuarios cerca: un BLE Companion y un repetidor puesto alto (`faq.md:169`).
5. **Gestión remota.** El repetidor se administra por RF desde la app (`faq.md:143`) o por USB con config.meshcore.io.
6. **Presencia en la red.** Un repetidor emite flood advert cada 12 h por defecto (`faq.md:209`). Los clientes sólo anuncian cuando el usuario lo pide.

## 2. Tradeoffs

| | Solo companion | Repetidor + companion |
|---|---|---|
| Costo y hardware | 1 dispositivo | 2 o más, más montaje |
| Alcance | Limitado al sitio del usuario | Mayor si el repetidor está bien ubicado |
| Puntos de falla | 1 | 2: si cae el repetidor, el companion queda sin red |
| Latencia | Sin salto extra | +1 salto |
| Portabilidad | Alta | El repetidor es fijo |
| Complejidad | Baja | Más (claves, nombres, ID, admin) |
| Mantenimiento | Casi nulo | Solar, batería, firmware, sitio |

## 3. Cuándo usar cada uno

- **Solo companion:** zona ya cubierta, uso móvil o de campo, pruebas, o repetidores cercanos de terceros.
- **Repetidor + companion:** mala cobertura (valle, interior), nodo estable (p. ej. companion USB en servidor), o aportar cobertura a la comunidad.

## 4. Cómo funciona el routing (por qué el repetidor no satura el aire)

**Topología de los ejemplos.** Dos companions y tres repetidores en cadena. Los hashes de 1 byte son inventados para ilustrar:

```
Ana (companion) ~~ R1[A3] ~~ R2[7F] ~~ R3[C1] ~~ Beto (companion)
```

Ana y Beto no se oyen directamente. Cada `~~` es un enlace LoRa. Hay un repetidor vecino, `R4[5E]`, que oye a R1 y a R2 pero no está en la cadena.

### 4.1 El primer mensaje a un destino va por flood

Ana nunca ha escrito a Beto, así que no tiene ruta (`faq.md:486`). El mensaje sale como flood: todos los repetidores que lo oyen lo retransmiten y cada uno añade su hash al path del paquete.

```
Ana  → flood, path=[]
R1   → retransmite, path=[A3]
R2   → retransmite, path=[A3,7F]      (R4 también lo oye y lo retransmite: path=[A3,5E])
R3   → retransmite, path=[A3,7F,C1]
Beto → recibe
```

- Un flood cubre todas las rutas posibles y por eso llega aunque no sepas el camino. El costo es airtime: R4 gasta TX aunque no sirva.
- El path crece con cada salto (máx. 64 bytes: `MAX_PATH_SIZE`, `src/MeshCore.h:22`).
- Cada repetidor espera un retardo aleatorio antes de retransmitir para no chocar con los vecinos (`cli_commands.md:528`).

### 4.2 El destino responde con un delivery report

Cuando Beto recibe el mensaje, devuelve un *delivery report* con la lista de repetidores recorridos. Ese reporte también va por flood de vuelta (`faq.md:486`). Ana lo guarda en su lista de contactos como la ruta hacia Beto.

```
Beto → report con path=[A3,7F,C1]  (flood de regreso hacia Ana)
Ana  → guarda en el contacto "Beto": ruta A3 → 7F → C1
```

- La ruta se guarda en la lista de contactos del **emisor** (`faq.md:486`).

### 4.3 Los mensajes siguientes llevan el path embebido

El segundo mensaje de Ana a Beto ya no es flood. Lleva `A3,7F,C1` escrito en el paquete y cada repetidor mira si le toca (`faq.md:486`).

```
Ana  → directo, path=[A3,7F,C1]
R1[A3] → su hash es el primero: retransmite
R2[7F] → su hash es el siguiente: retransmite
R3[C1] → retransmite
R4[5E] → oye el paquete pero su hash no está en el path: NO retransmite
Beto → recibe
```

- El ahorro de airtime es la diferencia: R4 queda en silencio y la red queda libre para otros.
- Si un repetidor del path desaparece (móvil, sin batería), el mensaje falla tras 3 reintentos. El cliente borra la ruta y en el último reintento vuelve a flood (por defecto, configurable en la app) (`faq.md:481`). Si la ruta pasa por un repetidor móvil, esperar roturas frecuentes.
- Se puede fijar el path a mano si conoces un repetidor concreto.

### 4.4 Los mensajes de canal siempre hacen flood

Un canal es de uno a muchos, así que no hay un destino único ni un path que aprender (`faq.md:491`). Cada mensaje de canal hace flood completo, sin importar si hay repetidores con rutas conocidas.

**Canal Public (por defecto).** Clave pública y conocida por todos: `8b3387e9c5cdea6ac9e5edbaa115cd72` (`docs/companion_protocol.md:436`, `faq.md:381`). Cualquiera que tenga la app lo oye y lo puede leer.

```
Ana escribe en Public: "¿Alguien en Bucaramanga?"
Ana → flood, path=[]
R1 → path=[A3] → R2 → path=[A3,7F] → R3 → path=[A3,7F,C1]
R4 → también retransmite
Beto, Carla y quien esté en rango → reciben y descifran con la clave pública
```

**Canal hashtag, por ejemplo `#test`.** La clave son los primeros 16 bytes de `sha256("#test")`: `9cd8fcf22a47333b591d96a2b848b73f` (`docs/companion_protocol.md:441`). El tráfico va cifrado en el aire, pero quien conozca o adivine el nombre puede derivar la clave, así que **no es privado** (`docs/companion_protocol.md:444`).

```
Ana escribe en #test: "ping"
Ana → flood, path=[]  (mismo mecanismo que Public)
Repetidores → retransmiten igual: no distinguen canales
Beto, si tiene #test → descifra
Carla, sin #test → recibe el paquete en el aire pero no puede leerlo
```

- **Para el repetidor, los dos canales son iguales:** ambos son paquetes `PAYLOAD_TYPE_GRP_TXT` por flood (`src/Packet.h:24`). Los repetidores no descifran el contenido: reenvían todo. Un hashtag no ahorra airtime respecto a Public.
- Lo que cambia es **quién puede leer**, y no el cómo viaja. El paquete lleva un *channel hash* de 1 byte (primer byte de SHA256 de la clave, `docs/payloads.md:233`) que el receptor usa para decidir si intenta descifrar.
- Un canal muy activo (Public) genera más flood que uno de nicho como `#test`, y ese volumen sí lo sienten los repetidores con `airtime factor` alto (`cli_commands.md:588`).
- Los admins de repetidores pueden recortar el flood de canal con `set flood.max <hops>` (`cli_commands.md:675`, `faq.md:491`). Un mensaje de Public a 10 saltos puede morir en un repetidor con `flood.max 8`.

### 4.5 Un repetidor no retransmite todo

A diferencia de otros sistemas LoRa mesh, MeshCore no reenvía todo lo que oye (`faq.md:141`). Un repetidor retransmite en estos casos:

| Paquete | ¿Retransmite? |
|---|---|
| Flood nuevo (mensaje de canal o primer mensaje directo) | Sí, con retardo aleatorio |
| Directo con path que contiene su hash en el turno que le toca | Sí |
| Directo con path que **no** lo incluye (como R4 en 4.3) | No |
| Flood duplicado que ya retransmitió | No (`wasSeen`/`markSeen`, `src/Mesh.cpp:101-102`; además `routeRecvPacket` solo reenvía lo no marcado "do not retransmit", `src/Mesh.cpp:346`) |
| Flood que supera `flood.max` | No (`examples/simple_repeater/MyMesh.cpp:444`) |
| Flood con su propio hash repetido más veces que `loop.detect` permite | No (`cli_commands.md:506`) |

Resultado práctico: el airtime de la red crece con el número de flood (canales, adverts, primeros mensajes), no con el número de mensajes directos ya enrutados.

## 5. Límites y costos reales

- **Hops:** máximo 64 (`MAX_PATH_SIZE`, `src/MeshCore.h:22`). Con hash de 2 bytes son 32 hops y con 3 bytes son 21 (`faq.md:299`).
- **Colisión de ID:** el primer byte de la clave pública identifica al repetidor en el path. Un choque no rompe el reenvío pero dificulta trazar rutas (`faq.md:275`). Al desplegar, elegir una clave libre, o única en unos 16 km.
- **Control de flood ajeno:** otros admins pueden limitar tu flood con `set flood.max` (`cli_commands.md:675`, `faq.md:491`).
- **Anti-loop (fw ≥ 1.14):** `loop.detect` descarta paquetes con el ID del repetidor ya repetido en el path (`cli_commands.md:506`). Un firmware custom defectuoso puede causar tormentas hasta 64 hops (`cli_commands.md:512`).
- **Contención local:** hay un retardo aleatorio antes de retransmitir (`cli_commands.md:528`) y un `airtime factor` que impone silencio tras cada TX (`cli_commands.md:588`). Varios repetidores en el mismo punto suman latencia y no cobertura.
- **Ruta perdida:** si un nodo aprendió una ruta por un repetidor móvil y desaparece, la ruta falla (`faq.md`, sección 5.3).
- **Room server:** puede repetir con `set repeat on`, pero no es recomendable. Lo mejor es separar repetidor y room server (`faq.md:154`).

## 6. Reglas prácticas

1. La ganancia viene de la **ubicación** del repetidor. Si lo pones junto al companion en el mismo sitio malo, no ganas casi nada.
2. No llenes la zona de repetidores. Más no es mejor: aumentan los reenvíos y la contención.
3. Si el companion ya llega directo a un repetidor de la comunidad, probablemente no necesitas poner uno propio.
4. Antes de desplegar: cambiar la contraseña de admin (por defecto `password`, `faq.md:254`) y configurar frecuencia y región (`faq.md:171`).
5. Para repetidores, usar firmware oficial ≥ 1.14 y activar `loop.detect` según el tamaño de hash.

## 7. Pendiente de confirmar

No hay respaldo en el repo para estas inferencias:

- Que el companion pueda bajar la potencia de TX por estar cerca de un repetidor, con el ahorro de batería que eso implicaría.
- El tamaño y comportamiento de la cola de mensajes offline del companion.
