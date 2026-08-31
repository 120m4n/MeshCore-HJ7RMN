---
marp: true
theme: default
paginate: true
---

# MeshCore
### Comunicación sin necesidad de internet ni celular

---

## ¿Qué es MeshCore?

- Firmware de código abierto para radios LoRa económicas
- Convierte un dispositivo de ~US$15–30 en un **nodo** de comunicación
- Se controla desde el celular con una app (Bluetooth o USB)
- No depende de torres celulares, WiFi ni internet

---

## ¿Cómo funciona? La idea de "red mesh"

- Cada nodo puede hablar directamente con los nodos cercanos
- Si el destino está lejos, los nodos intermedios **repiten el mensaje** (multi-salto)
- No hay un servidor central: si un nodo se apaga, el resto sigue funcionando
- Mientras más nodos hay alrededor, más lejos y más confiable llega la red

---

## Potencia de la señal: ¿por qué llega tan lejos?

- No es que transmita con mucha potencia — usa muy poca, similar a un router WiFi
- La clave es la modulación **LoRa** ("Long Range"): espectro ensanchado
- Sacrifica velocidad por alcance: manda poca información, muy "estirada" en el tiempo
- El receptor puede reconocer la señal aunque esté por debajo del ruido de fondo
- Resultado: kilómetros de alcance con una antena simple y poca batería

---

## Sin licencia, bajo costo

- Usa bandas de radio libres (ISM) — no requiere licencia como un radioaficionado
- Hardware desde ~US$15–30
- Cualquier persona puede armar un nodo o sumarse a una red ya existente

---

## Privacidad

- Los canales usan una clave compartida — solo quien la conoce puede leer los mensajes
- Los mensajes directos a un contacto van cifrados con su clave, de extremo a extremo
- El canal **Public** es la excepción: lo puede leer cualquiera con un nodo — no es privado

---

## Usos de la red

- Senderismo, camping, zonas sin cobertura celular
- Eventos masivos (conciertos, ferias) donde la red celular se satura
- Comunidades rurales o zonas remotas sin infraestructura
- Sensores y telemetría: temperatura, humedad, ubicación, alertas automáticas

---

## Sistema de emergencias

- Sigue funcionando cuando cae la infraestructura: cortes eléctricos, huracanes, terremotos, torres celulares caídas
- Los nodos pueden andar con batería o panel solar — siguen vivos días sin electricidad
- Útil para coordinar cuadrillas, brigadas comunitarias, protección civil local

### Límites honestos
- **No reemplaza al 911/bomberos** cuando sí hay señal celular — es un complemento para cuando no la hay
- Es texto y datos livianos, no llamadas de voz ni fotos o videos
- Necesita nodos cercanos para tener alcance — no es magia, es una red que crece con la comunidad

---

## Efecto de red: mientras más nodos, mejor

- Un nodo solo tiene el alcance de su propia radio (unos pocos kilómetros)
- Con vecinos que también tengan nodos, el mensaje "salta" más lejos
- Cada persona que se suma fortalece la red para todos — no es competencia, es colaboración

---

## ¿Cómo empezar?

1. Conseguí un dispositivo compatible (XIAO, Heltec, RAK, T-Echo, entre otros)
2. Instalá el firmware MeshCore
3. Descargá la app companion (Android/iOS)
4. Unite al canal de tu comunidad
5. ¡Listo! Ya sos parte de la red

---

# Gracias
### ¿Preguntas?
