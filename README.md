# Tostatronic — Video MJPEG en pantalla circular GC9A01 (ESP32-S3)

Al arrancar, el equipo muestra un splash **"Tostatronic"**, se conecta al WiFi,
descarga un archivo **.mjpeg** por HTTP a la **PSRAM** mostrando un anillo de
progreso con porcentaje real, y después reproduce el video en bucle (sin audio).

- Pantalla: GC9A01 1.28" 240×240 SPI (**Arduino_GFX**)
- Decodificación: **JPEGDEC** (un JPEG por fotograma)
- Placa: ESP32-S3 con PSRAM (probado con la configuración `esp32-s3-devkitc-1`)

Todo el código está en un único sketch: **`Tostatronic/Tostatronic.ino`**,
compatible a la vez con Arduino IDE y con PlatformIO.

## Cableado (pines por defecto, editables al inicio de `Tostatronic/Tostatronic.ino`)

| GC9A01 | ESP32-S3 | Nota                          |
|--------|----------|-------------------------------|
| VCC    | 3V3      |                               |
| GND    | GND      |                               |
| SCL    | GPIO 12  | reloj SPI (SCLK)              |
| SDA    | GPIO 11  | datos SPI (MOSI)              |
| DC     | GPIO 8   | dato/comando                  |
| CS     | GPIO 10  | chip select                   |
| RES    | GPIO 9   | reset                         |
| BLK    | GPIO 7   | backlight (o directo a 3V3 y pon `PIN_BL = -1`) |

## Configurar

Edita las constantes al inicio de `Tostatronic/Tostatronic.ino`:

```cpp
WIFI_SSID / WIFI_PASSWORD   // tu red WiFi
VIDEO_URL                   // p. ej. http://192.168.1.100:8000/video.mjpeg
MAX_VIDEO_SIZE              // 6 MB por defecto (módulos con 8 MB de PSRAM)
VIDEO_FPS                   // 15 por defecto; hazlo coincidir con el ffmpeg
```

Si tu módulo tiene PSRAM **quad** en lugar de **octal**, cambia en
`platformio.ini` la línea `board_build.arduino.memory_type = qio_opi` por
`qio_qspi` (si el Serial dice "PSRAM no encontrada", es esto).

## Generar el archivo .mjpeg con ffmpeg

Un `.mjpeg` es simplemente una concatenación de JPEGs. A partir de cualquier
video (`entrada.mp4`), escalado y recortado a 240×240 a 15 fps:

```bash
ffmpeg -i entrada.mp4 \
  -vf "fps=15,scale=240:240:force_original_aspect_ratio=increase,crop=240:240" \
  -c:v mjpeg -q:v 7 -an -f mjpeg video.mjpeg
```

- `-q:v` controla la calidad/tamaño: 2 = mejor calidad (archivo grande),
  10 = más comprimido. Con `-q:v 7` un clip de 30 s a 15 fps ronda 2–4 MB.
- `-an` elimina el audio (no se usa).
- Vigila que el archivo final quepa en `MAX_VIDEO_SIZE` (6 MB por defecto).

## Servir el archivo para probar

En la carpeta donde está `video.mjpeg`:

```bash
python -m http.server 8000
```

Averigua la IP de tu PC (`ipconfig` en Windows, `ip addr` en Linux) y pon en
`VIDEO_URL` algo como `http://192.168.1.100:8000/video.mjpeg`. El PC y la
ESP32 deben estar en la misma red. `http.server` envía `Content-Length`, así
que verás el porcentaje real; si tu servidor no lo envía, se muestra una
animación indeterminada con los KB recibidos.

## Compilar

**Arduino IDE:** abre `Tostatronic/Tostatronic.ino`, instala desde el Gestor
de Librerías *GFX Library for Arduino* (moononournation) y *JPEGDEC* (Larry
Bank), elige la placa **ESP32S3 Dev Module** y en Herramientas activa
**PSRAM: "OPI PSRAM"** (o "QSPI PSRAM" según tu módulo). Si no tienes el
soporte de ESP32, añade en Preferencias esta URL de gestor de placas:
`https://espressif.github.io/arduino-esp32/package_esp32_index.json`.

**PlatformIO:** abre la carpeta raíz del repo y ejecuta `pio run -t upload`,
luego `pio device monitor` (115200 baudios). El `platformio.ini` ya apunta al
sketch (`src_dir = Tostatronic`) e instala las librerías solo.

## Si el video es demasiado grande

El programa avisa por Serial y en pantalla si el archivo excede
`MAX_VIDEO_SIZE`. Opciones: subir el límite (si tu PSRAM lo permite),
recomprimir con `-q:v` más alto / menos fps, o usar la variante con tarjeta
microSD que está esbozada en un bloque de comentarios dentro del `.ino`.
