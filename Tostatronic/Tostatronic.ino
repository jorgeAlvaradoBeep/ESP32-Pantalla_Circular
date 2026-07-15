/**
 * ============================================================================
 *  Tostatronic — Reproductor de video MJPEG en pantalla circular GC9A01
 * ============================================================================
 *
 *  Hardware : ESP32-S3 (con PSRAM) + GC9A01 1.28" 240x240 SPI
 *  Flujo    : Splash "Tostatronic" -> WiFi -> descarga .mjpeg por HTTP a PSRAM
 *             (con barra de progreso circular) -> reproducción en bucle.
 *
 *  Formato de video: Motion JPEG "crudo" = fotogramas JPEG concatenados.
 *  Cada fotograma empieza con SOI (0xFF 0xD8) y termina con EOI (0xFF 0xD9).
 *  Ver README.md para generar el archivo con ffmpeg.
 *
 *  Librerías (Library Manager de Arduino IDE o lib_deps de PlatformIO):
 *    - "GFX Library for Arduino" (Arduino_GFX, de moononournation)
 *    - "JPEGDEC" (de bitbank2 / Larry Bank)
 *
 *  NOTA Arduino IDE: abre directamente este archivo (Tostatronic.ino). En
 *  Herramientas selecciona "ESP32S3 Dev Module" y activa "PSRAM: OPI PSRAM"
 *  (o "QSPI PSRAM" según tu módulo). Con PlatformIO no hay que hacer nada:
 *  platformio.ini ya apunta a esta carpeta.
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>

// ============================================================================
// CONFIGURACIÓN — edita esta sección
// ============================================================================

// --- WiFi -------------------------------------------------------------------
static const char *WIFI_SSID     = "SSID";
static const char *WIFI_PASSWORD = "PASS";
// Tiempo máximo de espera para conectar al WiFi (ms)
static const uint32_t WIFI_TIMEOUT_MS = 20000;

// --- Servidor de video --------------------------------------------------------
// URL del archivo MJPEG (HTTP plano; para probar: python -m http.server 8000)
static const char *VIDEO_URL = "http://192.168.100.104:8000/video.mjpeg";

// --- Pines SPI de la GC9A01 en la ESP32-S3 -----------------------------------
// Valores por defecto sobre el bus FSPI (pines "naturales" del S3).
// Cámbialos libremente: cualquier GPIO de salida sirve gracias al GPIO matrix.
static const int8_t PIN_SCLK = 12;   // SCL de la pantalla
static const int8_t PIN_MOSI = 11;   // SDA de la pantalla
static const int8_t PIN_DC   = 8;    // DC  (dato/comando)
static const int8_t PIN_CS   = 10;   // CS  (chip select)
static const int8_t PIN_RST  = 9;    // RES (reset)
static const int8_t PIN_BL   = 7;    // BLK (backlight). Pon -1 si va fijo a 3V3.

// Frecuencia SPI. La GC9A01 suele aguantar 80 MHz con cables cortos;
// si ves artefactos en pantalla, baja a 40000000.
static const uint32_t SPI_FREQ_HZ = 40000000;

// --- Memoria / video ----------------------------------------------------------
// Tamaño máximo del buffer de video en PSRAM (bytes).
// Un módulo N8R8 tiene 8 MB de PSRAM; dejamos margen para el sistema.
static const size_t MAX_VIDEO_SIZE = 6 * 1024 * 1024;   // 6 MB

// --- Reproducción -------------------------------------------------------------
// FPS objetivo. Debe coincidir (aprox.) con el fps con el que generaste el
// .mjpeg en ffmpeg. La ESP32-S3 decodifica JPEG 240x240 a ~25-30 fps.
static const float    VIDEO_FPS      = 15.0f;
static const uint32_t FRAME_INTERVAL = (uint32_t)(1000.0f / VIDEO_FPS);

// ============================================================================
// OBJETOS GLOBALES
// ============================================================================

// Bus SPI por hardware + panel GC9A01 (true = panel IPS)
Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_DC, PIN_CS, PIN_SCLK, PIN_MOSI,
                                            GFX_NOT_DEFINED /* sin MISO */);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, PIN_RST, 0 /* rotación */, true);

JPEGDEC jpeg;

// Buffer de video en PSRAM y tabla de fotogramas
static uint8_t *videoBuf   = nullptr;   // archivo .mjpeg completo
static size_t   videoSize  = 0;         // bytes realmente descargados

struct FrameRef {
  uint32_t offset;   // posición del SOI dentro de videoBuf
  uint32_t size;     // longitud del JPEG (SOI..EOI inclusive)
};
static FrameRef *frames     = nullptr;
static uint32_t  frameCount = 0;

// Colores del tema (RGB565)
static const uint16_t COL_BG     = RGB565_BLACK;
static const uint16_t COL_TITLE  = 0x07FF;  // cian
static const uint16_t COL_TEXT   = RGB565_WHITE;
static const uint16_t COL_RING_B = 0x2124;  // gris oscuro (fondo del anillo)
static const uint16_t COL_RING_F = 0x07E0;  // verde (progreso)
static const uint16_t COL_ERROR  = 0xF800;  // rojo

// Geometría de la pantalla
static const int16_t SCR_W = 240, SCR_H = 240;
static const int16_t CX = SCR_W / 2, CY = SCR_H / 2;

/*
 * ----------------------------------------------------------------------------
 * ALTERNATIVA CON TARJETA SD (para videos que no caben en PSRAM)
 * ----------------------------------------------------------------------------
 * Si el video excede MAX_VIDEO_SIZE, en lugar de PSRAM puedes descargarlo a
 * una microSD y reproducir leyendo de archivo. Esquema (sin probar, como guía):
 *
 *   #include <SD.h>
 *   #include <SPI.h>
 *   static const int8_t PIN_SD_CS = 21;          // CS de la SD (bus SPI aparte
 *   SPIClass sdSPI(HSPI);                        //  o compartido con la TFT)
 *   sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, PIN_SD_CS);
 *   SD.begin(PIN_SD_CS, sdSPI);
 *
 *   // Descarga: en vez de memcpy al buffer, File f = SD.open("/video.mjpeg",
 *   // FILE_WRITE); f.write(chunk, n); ...
 *
 *   // Reproducción: JPEGDEC soporta archivos con jpeg.open(path, cbOpen,
 *   // cbClose, cbRead, cbSeek, drawMCU) usando callbacks sobre File.
 *   // El escaneo SOI/EOI se hace leyendo el archivo por bloques.
 * ----------------------------------------------------------------------------
 */

// ============================================================================
// INTERFAZ GRÁFICA (splash, progreso, errores)
// ============================================================================

/** Imprime `txt` centrado horizontalmente con su línea base en y. */
static void printCentered(const char *txt, int16_t y, uint8_t size, uint16_t color)
{
  int16_t bx, by; uint16_t bw, bh;
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->getTextBounds(txt, 0, 0, &bx, &by, &bw, &bh);
  gfx->setCursor(CX - (int16_t)bw / 2 - bx, y);
  gfx->print(txt);
}

/** Dibuja la pantalla de splash: título + subtítulo + anillo vacío. */
static void drawSplash(void)
{
  gfx->fillScreen(COL_BG);
  printCentered("Tostatronic", 96, 3, COL_TITLE);
  printCentered("Cargando video...", 140, 1, COL_TEXT);
  // Anillo de fondo (progreso al 0%)
  gfx->fillArc(CX, CY, 116, 108, 0, 360, COL_RING_B);
}

/** Borra y reescribe la línea de estado pequeña (zona bajo el subtítulo). */
static void drawStatusLine(const char *txt, uint16_t color = COL_TEXT)
{
  gfx->fillRect(40, 152, 160, 14, COL_BG);
  printCentered(txt, 156, 1, color);
}

/**
 * Dibuja un arco del anillo partiendo de `startDeg` con barrido `sweepDeg`,
 * troceándolo para no pasar nunca de 360° (algunas versiones de fillArc no
 * normalizan ángulos mayores).
 */
static void ringArc(float startDeg, float sweepDeg, uint16_t color)
{
  if (sweepDeg >= 359.9f) {
    gfx->fillArc(CX, CY, 116, 108, 0, 360, color);
    return;
  }
  float a0 = fmodf(startDeg, 360.0f);
  float a1 = a0 + sweepDeg;
  if (a1 <= 360.0f) {
    gfx->fillArc(CX, CY, 116, 108, a0, a1, color);
  } else {
    gfx->fillArc(CX, CY, 116, 108, a0, 360, color);
    gfx->fillArc(CX, CY, 116, 108, 0, a1 - 360.0f, color);
  }
}

/**
 * Progreso determinado: anillo de -90° (arriba) en sentido horario + "NN %".
 * `pct` en [0..100].
 */
static void drawProgressRing(uint8_t pct)
{
  static int16_t lastEnd = -1;
  int16_t end = (int16_t)(pct * 3.6f);
  if (end != lastEnd) {
    // Si retrocede (nueva pantalla), repintamos el anillo de fondo
    if (end < lastEnd) ringArc(0, 360, COL_RING_B);
    if (end > 0)       ringArc(270, end, COL_RING_F);   // 270° = las 12 en punto
    lastEnd = end;
  }
  char buf[8];
  snprintf(buf, sizeof(buf), "%u %%", pct);
  gfx->fillRect(CX - 40, 168, 80, 20, COL_BG);
  printCentered(buf, 172, 2, COL_TEXT);
}

/**
 * Progreso indeterminado (sin Content-Length): arco de 60° girando.
 * Llamar periódicamente; internamente se autolimita a ~15 Hz.
 */
static void drawProgressSpinner(size_t bytes)
{
  static uint32_t lastMs = 0;
  static int16_t  angle  = 0;
  if (millis() - lastMs < 66) return;
  lastMs = millis();

  ringArc(0, 360, COL_RING_B);
  ringArc(angle, 60, COL_RING_F);
  angle = (angle + 18) % 360;

  // Con bytes == 0 (p. ej. durante la espera del WiFi) no mostramos contador
  if (bytes > 0) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u KB", (unsigned)(bytes / 1024));
    gfx->fillRect(CX - 50, 168, 100, 20, COL_BG);
    printCentered(buf, 172, 2, COL_TEXT);
  }
}

/** Error fatal: lo muestra en pantalla + Serial y detiene el programa. */
static void fatalError(const char *line1, const char *line2 = nullptr)
{
  Serial.printf("[ERROR] %s %s\n", line1, line2 ? line2 : "");
  gfx->fillScreen(COL_BG);
  gfx->fillArc(CX, CY, 116, 108, 0, 360, COL_ERROR);
  printCentered("ERROR", 90, 3, COL_ERROR);
  printCentered(line1, 130, 1, COL_TEXT);
  if (line2) printCentered(line2, 145, 1, COL_TEXT);
  printCentered("Reinicia el equipo", 170, 1, COL_RING_B);
  for (;;) delay(1000);   // detenido a propósito (ver Serial)
}

// ============================================================================
// WIFI
// ============================================================================

static void connectWiFi(void)
{
  Serial.printf("[WiFi] Conectando a \"%s\"...\n", WIFI_SSID);
  drawStatusLine("Conectando WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > WIFI_TIMEOUT_MS) {
      fatalError("WiFi no conectado", WIFI_SSID);
    }
    drawProgressSpinner(0);   // animación mientras espera
    delay(50);
  }
  Serial.printf("[WiFi] Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
  drawStatusLine("WiFi OK - Descargando");
}

// ============================================================================
// DESCARGA HTTP A PSRAM
// ============================================================================

static void downloadVideo(void)
{
  HTTPClient http;
  // HTTP/1.0 evita "Transfer-Encoding: chunked": así el stream crudo que
  // leemos es el archivo tal cual, sin cabeceras de trozos intercaladas.
  http.useHTTP10(true);
  http.setTimeout(15000);

  Serial.printf("[HTTP] GET %s\n", VIDEO_URL);
  if (!http.begin(VIDEO_URL)) {
    fatalError("URL invalida");
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    char msg[32];
    snprintf(msg, sizeof(msg), "HTTP %d", code);
    http.end();
    fatalError("Fallo de descarga", msg);
  }

  // Content-Length: -1 si el servidor no lo envía -> progreso indeterminado
  int contentLen = http.getSize();
  Serial.printf("[HTTP] Content-Length: %d\n", contentLen);

  if (contentLen > 0 && (size_t)contentLen > MAX_VIDEO_SIZE) {
    Serial.printf("[HTTP] El video ocupa %d bytes y el limite es %u.\n"
                  "       Sube MAX_VIDEO_SIZE (si tu PSRAM lo permite),\n"
                  "       recomprime el video, o usa la variante con SD\n"
                  "       comentada al inicio de este archivo.\n",
                  contentLen, (unsigned)MAX_VIDEO_SIZE);
    http.end();
    fatalError("Video demasiado grande", "ver monitor Serial");
  }

  // Reservamos el buffer en PSRAM (justo el necesario si conocemos el tamaño)
  size_t bufSize = (contentLen > 0) ? (size_t)contentLen : MAX_VIDEO_SIZE;
  videoBuf = (uint8_t *)ps_malloc(bufSize);
  if (!videoBuf) {
    http.end();
    fatalError("Sin memoria PSRAM", "ps_malloc fallo");
  }

  // Lectura del stream por bloques
  WiFiClient *stream = http.getStreamPtr();
  size_t   received   = 0;
  uint32_t lastDataMs = millis();
  uint32_t lastDrawMs = 0;

  while (http.connected() || stream->available()) {
    size_t avail = stream->available();
    if (avail) {
      if (received + avail > bufSize) {
        // Sin Content-Length el archivo puede superar el buffer en caliente
        Serial.printf("[HTTP] Excedido MAX_VIDEO_SIZE (%u bytes) durante la "
                      "descarga.\n", (unsigned)MAX_VIDEO_SIZE);
        http.end();
        fatalError("Video demasiado grande", "ver monitor Serial");
      }
      int n = stream->readBytes(videoBuf + received, avail);
      if (n <= 0) break;
      received  += n;
      lastDataMs = millis();

      // Refresco del progreso limitado a ~10 Hz para no frenar la descarga
      if (millis() - lastDrawMs > 100) {
        lastDrawMs = millis();
        if (contentLen > 0) {
          drawProgressRing((uint8_t)((uint64_t)received * 100 / contentLen));
        } else {
          drawProgressSpinner(received);
        }
      }
      if (contentLen > 0 && received >= (size_t)contentLen) break;
    } else {
      if (millis() - lastDataMs > 10000) {
        Serial.println("[HTTP] Timeout: 10 s sin datos.");
        break;
      }
      delay(1);
    }
  }
  http.end();

  if (received == 0) {
    fatalError("Descarga vacia", "revisa VIDEO_URL");
  }
  if (contentLen > 0 && received < (size_t)contentLen) {
    Serial.printf("[HTTP] Descarga incompleta: %u de %d bytes. Se intentara "
                  "reproducir lo recibido.\n", (unsigned)received, contentLen);
  }

  videoSize = received;
  drawProgressRing(100);
  Serial.printf("[HTTP] Descargados %u bytes. PSRAM libre: %u bytes.\n",
                (unsigned)videoSize, (unsigned)ESP.getFreePsram());
}

// ============================================================================
// INDEXADO DE FOTOGRAMAS (búsqueda de pares SOI/EOI)
// ============================================================================

static void indexFrames(void)
{
  // Estimación generosa de la tabla: 1 frame cada 2 KB como mínimo
  uint32_t maxFrames = (videoSize / 2048) + 16;
  frames = (FrameRef *)ps_malloc(maxFrames * sizeof(FrameRef));
  if (!frames) fatalError("Sin memoria PSRAM", "tabla de frames");

  frameCount = 0;
  size_t pos = 0;
  while (pos + 3 < videoSize && frameCount < maxFrames) {
    // SOI = FF D8; exigimos que el siguiente byte sea FF (inicio de otro
    // marcador JPEG) para descartar coincidencias casuales.
    if (videoBuf[pos] == 0xFF && videoBuf[pos + 1] == 0xD8 &&
        videoBuf[pos + 2] == 0xFF) {
      // Buscamos el EOI = FF D9. Dentro de los datos comprimidos los 0xFF
      // van "rellenados" como FF 00, así que FF D9 solo aparece como final.
      size_t end = pos + 2;
      while (end + 1 < videoSize &&
             !(videoBuf[end] == 0xFF && videoBuf[end + 1] == 0xD9)) {
        end++;
      }
      if (end + 1 >= videoSize) break;   // frame cortado al final: se ignora
      frames[frameCount].offset = pos;
      frames[frameCount].size   = (uint32_t)(end + 2 - pos);
      frameCount++;
      pos = end + 2;
    } else {
      pos++;
    }
  }

  Serial.printf("[MJPEG] Fotogramas encontrados: %u\n", frameCount);
  if (frameCount == 0) {
    fatalError("Archivo sin frames JPEG", "es un .mjpeg valido?");
  }
}

// ============================================================================
// REPRODUCCIÓN
// ============================================================================

/** Callback de JPEGDEC: vuelca cada bloque MCU decodificado a la pantalla. */
static int jpegDrawCallback(JPEGDRAW *pDraw)
{
  gfx->draw16bitBeRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels,
                            pDraw->iWidth, pDraw->iHeight);
  return 1;   // 1 = continuar decodificando
}

/** Decodifica y dibuja (centrado) el fotograma `idx`. */
static void drawFrame(uint32_t idx)
{
  if (!jpeg.openRAM(videoBuf + frames[idx].offset, frames[idx].size,
                    jpegDrawCallback)) {
    Serial.printf("[MJPEG] Frame %u corrupto (open), se salta.\n", idx);
    return;
  }
  // La pantalla espera RGB565 big-endian (lo maneja draw16bitBeRGBBitmap)
  jpeg.setPixelType(RGB565_BIG_ENDIAN);

  // Offset para centrar si el JPEG no es exactamente 240x240
  int16_t xOff = (SCR_W - jpeg.getWidth())  / 2;
  int16_t yOff = (SCR_H - jpeg.getHeight()) / 2;
  if (xOff < 0) xOff = 0;
  if (yOff < 0) yOff = 0;

  if (!jpeg.decode(xOff, yOff, 0)) {
    Serial.printf("[MJPEG] Frame %u corrupto (decode), se salta.\n", idx);
  }
  jpeg.close();
}

// ============================================================================
// SETUP / LOOP
// ============================================================================

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Tostatronic MJPEG Player ===");

  // Backlight (encendido simple; para atenuar podría usarse ledcAttach/ledcWrite)
  if (PIN_BL >= 0) {
    pinMode(PIN_BL, OUTPUT);
    digitalWrite(PIN_BL, HIGH);
  }

  // Pantalla
  if (!gfx->begin(SPI_FREQ_HZ)) {
    Serial.println("[ERROR] gfx->begin() fallo. Revisa los pines SPI.");
    for (;;) delay(1000);
  }

  // PSRAM: imprescindible para el buffer de video
  if (!psramFound()) {
    fatalError("PSRAM no encontrada", "revisa platformio.ini");
  }
  Serial.printf("[MEM] PSRAM total: %u bytes, libre: %u bytes\n",
                (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());

  drawSplash();       // 1) logo "Tostatronic" + "Cargando video..."
  connectWiFi();      // 2) WiFi con estado en pantalla
  downloadVideo();    // 3) descarga con barra de progreso circular
  indexFrames();      // 4) localizar todos los pares SOI/EOI

  // WiFi ya no hace falta: lo apagamos para liberar corriente y RAM
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  gfx->fillScreen(COL_BG);
  Serial.printf("[PLAY] Reproduciendo %u frames a %.1f fps.\n",
                frameCount, VIDEO_FPS);
}

void loop()
{
  static uint32_t frameIdx    = 0;
  static uint32_t nextFrameMs = 0;

  // Temporización no bloqueante: solo dibujamos cuando toca el siguiente frame
  uint32_t now = millis();
  if (now < nextFrameMs) return;

  // Programamos el siguiente tick; si vamos atrasados (decodificación lenta),
  // reanclamos a "ahora" para no acumular retraso.
  nextFrameMs += FRAME_INTERVAL;
  if (nextFrameMs <= now) nextFrameMs = now + FRAME_INTERVAL;

  drawFrame(frameIdx);
  frameIdx = (frameIdx + 1) % frameCount;   // bucle infinito del video
}
