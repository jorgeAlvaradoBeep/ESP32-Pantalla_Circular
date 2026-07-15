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
- `Config.h`: constantes de pines, intervalos y endpoint base por defecto.

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
