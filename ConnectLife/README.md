# ConnectLife ESP32-S3 Controller

Proyecto modular para controlar un aire acondicionado Hisense con ConnectLife desde una interfaz web alojada en una ESP32-S3, compilando directamente desde Arduino IDE.

## Requisitos

- Arduino IDE 2.x
- Core `esp32` para Arduino
- Librerías:
  - `ArduinoJson`
  - `DHT sensor library`
  - `Adafruit Unified Sensor`
  - `GFX Library for Arduino`

`JPEGDEC` solo es necesaria si vuelves a integrar reproducción MJPEG; para los avisos de estado en la pantalla circular no se usa.

## Estructura

- `ConnectLife.ino`: arranque, WiFi, loop principal y coordinación.
- `ConnectLifeClient.*`: login, refresh token, Device ID, estado y comandos ConnectLife.
- `WebPortal.*`: servidor web, AJAX, configuración, logs y OTA por navegador.
- `CircularDisplay.*`: avisos en la pantalla circular GC9A01.
- `AppConfig.*`: configuración persistente en Preferences/NVS.
- `Sensor.*`: capa desacoplada para DHT11; se puede sustituir por SHT31 sin tocar la UI.
- `AppLogger.*`: logs en memoria para `/config`.
- `TempControl.*`: lazo autónomo de temperatura (PI + horarios).
- `ControlTypes.h`: tipos compartidos del control (`Schedule`, `ControlConfig`).
- `Config.h`: constantes de pines, intervalos, ganancias del control y endpoint base por defecto.

## Compilación

El esquema de particiones por defecto (4 MB) deja el sketch al 97 % del espacio de
app y no da margen para OTA. Compila con el esquema de 8 MB:

```
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi" ConnectLife
```

Con eso el uso baja al 38 % (1.29 MB de 3.34 MB) y quedan las dos particiones de app
que el OTA necesita. En Arduino IDE: *Tools > Flash Size: 8MB* y
*Partition Scheme: 8M with spiffs (3MB APP/1.5MB SPIFFS)*.

## Control autónomo de temperatura

Página `/control`. El equipo solo acepta un setpoint entero y regula contra su
propio sensor, que está dentro de la unidad y no donde está el usuario. Por eso
el firmware no le reenvía el objetivo tal cual: mide con el sensor local, calcula
el error contra lo que pidió el usuario y le manda un **setpoint virtual sesgado**
hasta que el cuarto llega de verdad a la temperatura pedida.

- **Lazo PI** (`CONTROL_KP`, `CONTROL_KI` en `Config.h`), sesgo limitado a ±5 °C.
- **Objetivo del usuario**: 18-25 °C. **Setpoint enviado al equipo**: 16-30 °C.
- **Evaluación** cada 60 s sobre una media móvil de 60 s del sensor.
- **Banda muerta de 0.8 °C**, dictada por el DHT11 (±2 °C de exactitud, 1 °C de
  resolución). Con un SHT31/SHT40 puede bajarse a ~0.3 °C y ahí el control pasa a
  ser realmente preciso.
- **Mínimo 10 min entre escrituras** al equipo, para proteger el compresor de
  ciclos cortos y no saturar la nube. No aplica al primer comando de un bloque.

### Prioridades

1. **Petición instantánea** (`Aplicar ahora`) — pisa cualquier horario. Se cancela
   sola cuando arranca el siguiente bloque programado.
2. **Horario programado** — hasta 3 bloques, con máscara de días y objetivo propio.
   Si el fin es anterior al inicio, el bloque cruza medianoche y los días marcados
   son los del arranque.
3. **Sin nada activo** — si el control había encendido el equipo, lo apaga.

Tocar el aire en crudo desde `/` (setpoint, modo, ventilador, encendido) **apaga el
control autónomo**, para que los dos no se peleen por el setpoint. Se reactiva desde
`/control`.

### Hora local

Los horarios usan hora de pared, no UTC. La zona se configura en `/config` como
cadena POSIX TZ (por defecto `CST6`, México central sin horario de verano). Sin
sincronización NTP los horarios no se evalúan; la petición instantánea sí funciona.

## Pines

El DHT11 está configurado en `GPIO4` en `Config.h`.

La pantalla circular GC9A01 usa estos pines por defecto:

- `SCLK`: GPIO12
- `MOSI`: GPIO11
- `DC`: GPIO8
- `CS`: GPIO10
- `RST`: GPIO9
- `BL`: GPIO7

Puedes cambiarlos en `Config.h`.

## Uso

1. Abre `ConnectLife.ino` desde Arduino IDE.
2. Selecciona tu placa ESP32-S3 y compila.
3. Instala las librerías requeridas desde Library Manager.
4. Sube el firmware.
5. Si no hay WiFi configurado, la ESP32 crea el AP `ConnectLife-Setup` con contraseña `connectlife`.
6. Entra a `/config`, guarda WiFi y credenciales ConnectLife.
7. Pulsa `Login`.

## ConnectLife

La lógica HTTP está completamente encapsulada en `ConnectLifeClient`. El endpoint base se guarda en NVS y puede cambiarse desde `/config`.

ConnectLife/Hisense no ofrece una API pública oficial estable para Arduino. Esta versión integra el flujo descrito por el proyecto open source `bilan/connectlife-api-connector`, que fue construido mediante ingeniería inversa de la app móvil ConnectLife.

Flujo implementado:

- `POST https://accounts.eu1.gigya.com/accounts.login`
- `POST https://accounts.eu1.gigya.com/accounts.getJWT`
- `POST https://oauth.hijuconn.com/oauth/authorize`
- `POST https://oauth.hijuconn.com/oauth/token`
- `GET https://clife-eu-gateway.hijuconn.com/clife-svc/pu/get_device_status_list`
- `POST https://clife-eu-gateway.hijuconn.com/device/pu/property/set`

Los comandos usan propiedades reales del AC:

- `t_power`: encendido/apagado.
- `t_temp`: temperatura objetivo.
- `t_work_mode`: fan, heat, cool, dry, auto.
- `t_fan_speed`: auto, low, medium, high.
- `t_swing_direction` / `t_swing_angle`: swing.
- `t_sleep`: sleep.
- `t_super`: turbo.

Las llamadas Hijuconn se firman con SHA-256 + RSA usando la llave pública y el formato de payload documentado por el repo de referencia. Si en tu región cambia el gateway o el esquema, solo debería tocarse `ConnectLifeClient.cpp`; el `.ino`, UI, sensor, NVS y OTA no cambian.

## OTA

La actualización de firmware se realiza desde `/config` mediante formulario web y `Update.h`. No utiliza ArduinoOTA.

## Seguridad

Las credenciales se guardan en NVS mediante `Preferences`, no en EEPROM ni en archivos del sketch. Para un producto final conviene añadir autenticación local al panel web y fijar certificados TLS en lugar de `client.setInsecure()`.
