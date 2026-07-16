// DemoPantalla — vitrina de la pantalla circular GC9A01 (240x240)
//
// 10 escenas que rotan automáticamente cada 10 segundos, 9 de ellas animadas:
//   1. Intro con anillos giratorios        (animada)
//   2. Velocímetro con aguja suavizada     (animada)
//   3. Carga del sistema con porcentaje    (animada)
//   4. Panel de sensores simulados         (animada)
//   5. Osciloscopio / onda en tiempo real  (animada)
//   6. Reloj analógico + digital           (animada)
//   7. Ecualizador de audio                (animada)
//   8. Radar con ecos                      (animada)
//   9. Plasma a color (imagen procedural)  (animada)
//  10. Mandala generativo (imagen fija)    (estática)
//
// Usa la MISMA conexión de pantalla que el proyecto ConnectLife (Config.h).

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <math.h>

// --- Conexión de la pantalla (idéntica a ConnectLife/Config.h) -------------
static const int8_t DISPLAY_PIN_SCLK = 12;
static const int8_t DISPLAY_PIN_MOSI = 11;
static const int8_t DISPLAY_PIN_DC = 8;
static const int8_t DISPLAY_PIN_CS = 10;
static const int8_t DISPLAY_PIN_RST = 9;
static const int8_t DISPLAY_PIN_BL = 7;
static const uint32_t DISPLAY_SPI_FREQ_HZ = 40000000;

Arduino_DataBus *bus = new Arduino_ESP32SPI(DISPLAY_PIN_DC,
                                            DISPLAY_PIN_CS,
                                            DISPLAY_PIN_SCLK,
                                            DISPLAY_PIN_MOSI,
                                            GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, DISPLAY_PIN_RST, 0, true);

// --- Constantes generales ---------------------------------------------------
static const int16_t SCR = 240;
static const int16_t CX = SCR / 2;
static const int16_t CY = SCR / 2;
static const uint32_t SCENE_MS = 10000;
static const uint8_t SCENE_COUNT = 10;

static const uint16_t COL_BG = RGB565_BLACK;
static const uint16_t COL_TEXT = RGB565_WHITE;
static const uint16_t COL_MUTED = 0x8410;
static const uint16_t COL_CYAN = 0x07FF;
static const uint16_t COL_GREEN = 0x07E0;
static const uint16_t COL_DIMGREEN = 0x0280;
static const uint16_t COL_YELLOW = 0xFFE0;
static const uint16_t COL_ORANGE = 0xFD20;
static const uint16_t COL_RED = 0xF800;
static const uint16_t COL_BLUE = 0x34DF;
static const uint16_t COL_RINGBG = 0x2124;
static const uint16_t COL_PLOTBG = 0x0841;

static uint8_t sceneIndex = 0;
static uint32_t sceneStartMs = 0;
static uint16_t plasmaPalette[64];

// --- Utilidades --------------------------------------------------------------
static void printCentered(const String &text, int16_t y, uint8_t size, uint16_t color)
{
  int16_t bx, by;
  uint16_t bw, bh;
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->getTextBounds(text.c_str(), 0, 0, &bx, &by, &bw, &bh);
  gfx->setCursor(CX - (int16_t)bw / 2 - bx, y);
  gfx->print(text.c_str());
}

static void polar(float deg, float radius, int16_t &x, int16_t &y)
{
  const float rad = deg * DEG_TO_RAD;
  x = CX + (int16_t)lroundf(radius * cosf(rad));
  y = CY + (int16_t)lroundf(radius * sinf(rad));
}

// Arco en anillo que admite cruzar 360 grados (0 = a las 3, 270 = arriba)
static void ringArc(float startDeg, float sweepDeg, int16_t rOut, int16_t rIn, uint16_t color)
{
  if (sweepDeg < 0.5f) return;
  if (sweepDeg >= 359.9f) {
    gfx->fillArc(CX, CY, rOut, rIn, 0, 360, color);
    return;
  }
  float a0 = fmodf(startDeg, 360.0f);
  float a1 = a0 + sweepDeg;
  if (a1 <= 360.0f) {
    gfx->fillArc(CX, CY, rOut, rIn, a0, a1, color);
  } else {
    gfx->fillArc(CX, CY, rOut, rIn, a0, 360, color);
    gfx->fillArc(CX, CY, rOut, rIn, 0, a1 - 360.0f, color);
  }
}

static void drawSceneBadge(const char *title)
{
  printCentered(title, 24, 1, COL_MUTED);
  printCentered(String(sceneIndex + 1) + "/" + String(SCENE_COUNT), 208, 1, COL_MUTED);
}

// ============================================================================
// Escena 1: intro
// ============================================================================
static int16_t introAngle;
static uint32_t introLastMs;

static void introInit()
{
  gfx->fillScreen(COL_BG);
  printCentered("TOSTATRONIC", 96, 2, COL_CYAN);
  printCentered("Demo pantalla circular", 130, 1, COL_TEXT);
  printCentered("GC9A01 240x240", 148, 1, COL_MUTED);
  drawSceneBadge("INTRO");
  introAngle = 0;
  introLastMs = 0;
}

static void introUpdate(uint32_t now)
{
  if (now - introLastMs < 60) return;
  introLastMs = now;
  ringArc(0, 360, 118, 110, COL_RINGBG);
  ringArc(introAngle, 55, 118, 110, COL_CYAN);
  ringArc(introAngle + 180, 55, 118, 110, COL_ORANGE);
  introAngle = (introAngle + 12) % 360;
}

// ============================================================================
// Escena 2: velocímetro
// ============================================================================
static float gaugeValue, gaugeTarget;
static uint32_t gaugeNextTargetMs, gaugeLastMs;
static int gaugeShown;

static void gaugeInit()
{
  gfx->fillScreen(COL_BG);
  drawSceneBadge("VELOCIMETRO");
  for (int v = 0; v <= 180; v += 30) {
    const float ang = 135.0f + (v / 180.0f) * 270.0f;
    int16_t x0, y0, x1, y1;
    polar(ang, 98, x0, y0);
    polar(ang, 112, x1, y1);
    gfx->drawLine(x0, y0, x1, y1, COL_MUTED);
  }
  printCentered("km/h", 138, 1, COL_MUTED);
  gaugeValue = 0;
  gaugeTarget = 120;
  gaugeNextTargetMs = 0;
  gaugeLastMs = 0;
  gaugeShown = -1;
}

static void gaugeUpdate(uint32_t now)
{
  if (now - gaugeLastMs < 40) return;
  gaugeLastMs = now;
  if (now >= gaugeNextTargetMs) {
    gaugeTarget = random(0, 181);
    gaugeNextTargetMs = now + random(1500, 3500);
  }
  gaugeValue += (gaugeTarget - gaugeValue) * 0.07f;

  const float sweep = (gaugeValue / 180.0f) * 270.0f;
  const uint16_t color = gaugeValue < 90 ? COL_GREEN : (gaugeValue < 140 ? COL_YELLOW : COL_RED);
  ringArc(135, sweep, 118, 104, color);
  ringArc(135 + sweep, 270 - sweep, 118, 104, COL_RINGBG);

  const int shown = (int)lroundf(gaugeValue);
  if (shown != gaugeShown) {
    gaugeShown = shown;
    gfx->fillRect(76, 84, 88, 36, COL_BG);
    printCentered(String(shown), 90, 4, COL_TEXT);
  }
}

// ============================================================================
// Escena 3: carga del sistema
// ============================================================================
static float loadPct;
static uint32_t loadLastMs, loadDoneMs;
static int loadShown;
static int8_t loadPhase;
static const char *LOAD_STEPS[] = {"Descargando datos", "Instalando modulos", "Verificando", "Completado"};

static void loadInit()
{
  gfx->fillScreen(COL_BG);
  drawSceneBadge("CARGA");
  ringArc(0, 360, 114, 104, COL_RINGBG);
  loadPct = 0;
  loadShown = -1;
  loadPhase = -1;
  loadDoneMs = 0;
  loadLastMs = 0;
}

static void loadUpdate(uint32_t now)
{
  if (now - loadLastMs < 60) return;
  loadLastMs = now;

  if (loadPct >= 100.0f) {
    if (loadDoneMs == 0) loadDoneMs = now;
    if (now - loadDoneMs > 1500) loadInit();  // reinicia el ciclo
    return;
  }

  loadPct += random(2, 11) / 10.0f;
  if (loadPct > 100.0f) loadPct = 100.0f;

  ringArc(270, loadPct * 3.6f, 114, 104, COL_CYAN);

  const int shown = (int)loadPct;
  if (shown != loadShown) {
    loadShown = shown;
    gfx->fillRect(70, 88, 100, 28, COL_BG);
    printCentered(String(shown) + "%", 92, 3, COL_TEXT);
  }

  const int8_t phase = loadPct >= 100 ? 3 : (loadPct > 66 ? 2 : (loadPct > 33 ? 1 : 0));
  if (phase != loadPhase) {
    loadPhase = phase;
    gfx->fillRect(30, 138, 180, 14, COL_BG);
    printCentered(LOAD_STEPS[phase], 140, 1, phase == 3 ? COL_GREEN : COL_MUTED);
  }
}

// ============================================================================
// Escena 4: sensores simulados
// ============================================================================
static uint32_t sensLastMs;

static void sensorRow(int16_t y, const char *label, const String &value, float frac, uint16_t color)
{
  gfx->setTextSize(1);
  gfx->setTextColor(COL_MUTED);
  gfx->setCursor(52, y);
  gfx->print(label);

  gfx->fillRect(130, y - 2, 62, 12, COL_BG);
  gfx->setTextColor(COL_TEXT);
  gfx->setCursor(132, y);
  gfx->print(value);

  frac = constrain(frac, 0.0f, 1.0f);
  const int16_t w = (int16_t)(136 * frac);
  gfx->fillRect(52, y + 12, w, 8, color);
  gfx->fillRect(52 + w, y + 12, 136 - w, 8, COL_RINGBG);
}

static void sensorsInit()
{
  gfx->fillScreen(COL_BG);
  drawSceneBadge("SENSORES");
  printCentered("Lecturas en vivo", 44, 1, COL_TEXT);
  sensLastMs = 0;
}

static void sensorsUpdate(uint32_t now)
{
  if (now - sensLastMs < 200) return;
  sensLastMs = now;
  const float t = now / 1000.0f;

  const float temp = 24.0f + 5.0f * sinf(t * 0.35f) + random(-3, 4) / 10.0f;
  const float hum = 55.0f + 16.0f * sinf(t * 0.19f + 2.1f) + random(-5, 6) / 10.0f;
  const float lux = 480.0f + 380.0f * sinf(t * 0.12f + 4.4f) + random(-8, 9);

  sensorRow(78, "Temp", String(temp, 1) + " C", (temp - 15.0f) / 20.0f, COL_ORANGE);
  sensorRow(118, "Hum ", String((int)hum) + " %", hum / 100.0f, COL_BLUE);
  sensorRow(158, "Luz ", String((int)lux) + " lx", lux / 900.0f, COL_YELLOW);
}

// ============================================================================
// Escena 5: osciloscopio
// ============================================================================
static uint32_t waveLastMs;
static float wavePhase;

static void waveInit()
{
  gfx->fillScreen(COL_BG);
  drawSceneBadge("OSCILOSCOPIO");
  printCentered("Senal simulada", 190, 1, COL_MUTED);
  gfx->drawRect(38, 64, 164, 112, COL_RINGBG);
  waveLastMs = 0;
  wavePhase = 0;
}

static void waveUpdate(uint32_t now)
{
  if (now - waveLastMs < 45) return;
  waveLastMs = now;
  wavePhase += 0.25f;

  gfx->fillRect(40, 66, 160, 108, COL_PLOTBG);
  for (int16_t x = 48; x < 200; x += 10) {
    gfx->drawPixel(x, 120, COL_MUTED);
  }

  const float amp = 34.0f + 14.0f * sinf(wavePhase * 0.13f);
  int16_t prevY = 0;
  for (int16_t i = 0; i < 160; i++) {
    const float v = sinf(wavePhase + i * 0.09f) + 0.35f * sinf(wavePhase * 1.7f + i * 0.23f);
    int16_t y = 120 - (int16_t)(amp * v * 0.6f) + random(-1, 2);
    y = constrain(y, 67, 172);
    if (i > 0) {
      gfx->drawLine(40 + i - 1, prevY, 40 + i, y, COL_GREEN);
    }
    prevY = y;
  }
}

// ============================================================================
// Escena 6: reloj analógico
// ============================================================================
static uint32_t clockLastMs;
static int16_t prevSecX, prevSecY, prevMinX, prevMinY, prevHourX, prevHourY;
static const uint32_t CLOCK_BASE_SECONDS = 10UL * 3600 + 8UL * 60 + 20;  // 10:08:20

static void clockInit()
{
  gfx->fillScreen(COL_BG);
  drawSceneBadge("RELOJ");
  for (int h = 0; h < 12; h++) {
    int16_t x0, y0, x1, y1;
    polar(h * 30.0f, 102, x0, y0);
    polar(h * 30.0f, 112, x1, y1);
    gfx->drawLine(x0, y0, x1, y1, h % 3 == 0 ? COL_TEXT : COL_MUTED);
  }
  clockLastMs = 0;
  prevSecX = prevMinX = prevHourX = CX;
  prevSecY = prevMinY = prevHourY = CY;
}

static void clockUpdate(uint32_t now)
{
  if (now - clockLastMs < 200) return;
  clockLastMs = now;

  const uint32_t total = CLOCK_BASE_SECONDS + now / 1000;
  const uint8_t s = total % 60;
  const uint8_t m = (total / 60) % 60;
  const uint8_t h = (total / 3600) % 12;

  gfx->drawLine(CX, CY, prevHourX, prevHourY, COL_BG);
  gfx->drawLine(CX, CY, prevMinX, prevMinY, COL_BG);
  gfx->drawLine(CX, CY, prevSecX, prevSecY, COL_BG);

  int16_t x, y;
  polar(h * 30.0f + m * 0.5f - 90.0f, 52, x, y);
  gfx->drawLine(CX, CY, x, y, COL_TEXT);
  prevHourX = x; prevHourY = y;

  polar(m * 6.0f + s * 0.1f - 90.0f, 78, x, y);
  gfx->drawLine(CX, CY, x, y, COL_CYAN);
  prevMinX = x; prevMinY = y;

  polar(s * 6.0f - 90.0f, 92, x, y);
  gfx->drawLine(CX, CY, x, y, COL_RED);
  prevSecX = x; prevSecY = y;

  gfx->fillCircle(CX, CY, 4, COL_ORANGE);

  char digital[10];
  snprintf(digital, sizeof(digital), "%02u:%02u:%02u", h == 0 ? 12 : h, m, s);
  gfx->fillRect(88, 158, 64, 12, COL_BG);
  printCentered(digital, 160, 1, COL_MUTED);
}

// ============================================================================
// Escena 7: ecualizador
// ============================================================================
static const uint8_t EQ_BARS = 12;
static float eqHeight[EQ_BARS], eqTarget[EQ_BARS];
static uint32_t eqLastMs;

static void eqInit()
{
  gfx->fillScreen(COL_BG);
  drawSceneBadge("AUDIO");
  printCentered("Ecualizador", 190, 1, COL_MUTED);
  gfx->drawFastHLine(34, 176, 172, COL_MUTED);
  for (uint8_t i = 0; i < EQ_BARS; i++) {
    eqHeight[i] = 0;
    eqTarget[i] = random(10, 96);
  }
  eqLastMs = 0;
}

static void eqUpdate(uint32_t now)
{
  if (now - eqLastMs < 45) return;
  eqLastMs = now;

  for (uint8_t i = 0; i < EQ_BARS; i++) {
    if (fabsf(eqHeight[i] - eqTarget[i]) < 3.0f || random(0, 100) < 8) {
      eqTarget[i] = random(6, 96);
    }
    eqHeight[i] += (eqTarget[i] - eqHeight[i]) * 0.35f;

    const int16_t x = 36 + i * 14;
    const int16_t hNew = (int16_t)eqHeight[i];
    const uint16_t color = hNew > 72 ? COL_RED : (hNew > 44 ? COL_YELLOW : COL_GREEN);
    gfx->fillRect(x, 174 - 96, 12, 96 - hNew, COL_BG);
    gfx->fillRect(x, 174 - hNew, 12, hNew, color);
  }
}

// ============================================================================
// Escena 8: radar
// ============================================================================
struct RadarBlip {
  float angleDeg;
  float radius;
  uint32_t hitMs;
};
static RadarBlip blips[5];
static float radarAngle;
static uint32_t radarLastMs;

static void radarRespawn(RadarBlip &blip)
{
  blip.angleDeg = random(0, 360);
  blip.radius = random(24, 92);
  blip.hitMs = 0;
}

static void radarChrome()
{
  gfx->drawCircle(CX, CY, 30, COL_DIMGREEN);
  gfx->drawCircle(CX, CY, 60, COL_DIMGREEN);
  gfx->drawCircle(CX, CY, 94, COL_DIMGREEN);
  gfx->drawFastHLine(CX - 94, CY, 188, COL_DIMGREEN);
  gfx->drawFastVLine(CX, CY - 94, 188, COL_DIMGREEN);
}

static void radarInit()
{
  gfx->fillScreen(COL_BG);
  drawSceneBadge("RADAR");
  radarChrome();
  radarAngle = 0;
  radarLastMs = 0;
  for (RadarBlip &blip : blips) {
    radarRespawn(blip);
  }
}

static void radarUpdate(uint32_t now)
{
  if (now - radarLastMs < 35) return;
  radarLastMs = now;

  int16_t x, y;
  polar(radarAngle, 93, x, y);
  gfx->drawLine(CX, CY, x, y, COL_BG);        // borra el barrido anterior
  radarChrome();

  radarAngle = fmodf(radarAngle + 5.0f, 360.0f);
  polar(radarAngle, 93, x, y);
  gfx->drawLine(CX, CY, x, y, COL_GREEN);

  for (RadarBlip &blip : blips) {
    float diff = fabsf(radarAngle - blip.angleDeg);
    if (diff > 180) diff = 360 - diff;
    if (diff < 4.0f) blip.hitMs = now;

    if (blip.hitMs == 0) continue;
    const uint32_t age = now - blip.hitMs;
    int16_t bx, by;
    polar(blip.angleDeg, blip.radius, bx, by);
    if (age < 900) {
      gfx->fillCircle(bx, by, 3, COL_GREEN);
    } else if (age < 1800) {
      gfx->fillCircle(bx, by, 3, COL_DIMGREEN);
    } else {
      gfx->fillCircle(bx, by, 3, COL_BG);
      radarRespawn(blip);
    }
  }
}

// ============================================================================
// Escena 9: plasma (imagen procedural animada)
// ============================================================================
static float plasmaTime;
static uint32_t plasmaLastMs;

static void plasmaInit()
{
  gfx->fillScreen(COL_BG);
  plasmaTime = 0;
  plasmaLastMs = 0;
}

static void plasmaUpdate(uint32_t now)
{
  if (now - plasmaLastMs < 70) return;
  plasmaLastMs = now;
  plasmaTime += 0.18f;

  const int16_t block = 8;
  for (int16_t by = 0; by < SCR / block; by++) {
    for (int16_t bx = 0; bx < SCR / block; bx++) {
      const int16_t px = bx * block + block / 2;
      const int16_t py = by * block + block / 2;
      const float dx = px - CX;
      const float dy = py - CY;
      if (dx * dx + dy * dy > 118L * 118L) continue;  // fuera del circulo visible

      const float v = sinf(px * 0.045f + plasmaTime)
                    + sinf(py * 0.038f - plasmaTime * 1.3f)
                    + sinf((px + py) * 0.026f + plasmaTime * 0.7f)
                    + sinf(sqrtf(dx * dx + dy * dy) * 0.05f - plasmaTime);
      const uint8_t idx = (uint8_t)((v + 4.0f) * 7.9f) & 63;
      gfx->fillRect(bx * block, by * block, block, block, plasmaPalette[idx]);
    }
  }
}

// ============================================================================
// Escena 10: mandala generativo (imagen estática)
// ============================================================================
static void mandalaInit()
{
  gfx->fillScreen(COL_BG);

  for (int i = 0; i < 12; i++) {
    int16_t x, y;
    polar(i * 30.0f, 58, x, y);
    gfx->drawCircle(x, y, 40, plasmaPalette[(i * 5) & 63]);
  }
  for (int i = 0; i < 24; i++) {
    int16_t x0, y0, x1, y1;
    polar(i * 15.0f, 18, x0, y0);
    polar(i * 15.0f + 90.0f, 100, x1, y1);
    gfx->drawLine(x0, y0, x1, y1, COL_RINGBG);
  }
  for (int i = 0; i < 36; i++) {
    int16_t x, y;
    polar(i * 10.0f, 112, x, y);
    gfx->fillCircle(x, y, 2, plasmaPalette[(i * 2 + 20) & 63]);
  }
  gfx->fillCircle(CX, CY, 12, COL_CYAN);
  gfx->drawCircle(CX, CY, 16, COL_TEXT);

  drawSceneBadge("MANDALA");
}

static void mandalaUpdate(uint32_t)
{
  // Escena estática: la imagen se dibuja una sola vez en mandalaInit().
}

// ============================================================================
// Gestor de escenas
// ============================================================================
struct Scene {
  void (*init)();
  void (*update)(uint32_t now);
};

static const Scene SCENES[SCENE_COUNT] = {
    {introInit, introUpdate},
    {gaugeInit, gaugeUpdate},
    {loadInit, loadUpdate},
    {sensorsInit, sensorsUpdate},
    {waveInit, waveUpdate},
    {clockInit, clockUpdate},
    {eqInit, eqUpdate},
    {radarInit, radarUpdate},
    {plasmaInit, plasmaUpdate},
    {mandalaInit, mandalaUpdate},
};

void setup()
{
  Serial.begin(115200);
  randomSeed(micros());

  if (DISPLAY_PIN_BL >= 0) {
    pinMode(DISPLAY_PIN_BL, OUTPUT);
    digitalWrite(DISPLAY_PIN_BL, HIGH);
  }

  if (!gfx->begin(DISPLAY_SPI_FREQ_HZ)) {
    Serial.println("La pantalla no inicializo");
    while (true) delay(1000);
  }

  for (int i = 0; i < 64; i++) {
    const float a = i * (TWO_PI / 64.0f);
    const uint8_t r = 127 + (int8_t)(127.0f * sinf(a));
    const uint8_t g = 127 + (int8_t)(127.0f * sinf(a + 2.094f));
    const uint8_t b = 127 + (int8_t)(127.0f * sinf(a + 4.188f));
    plasmaPalette[i] = gfx->color565(r, g, b);
  }

  sceneIndex = 0;
  sceneStartMs = millis();
  SCENES[sceneIndex].init();
  Serial.println("Demo iniciada: 10 escenas, cambio cada 10 s");
}

void loop()
{
  const uint32_t now = millis();
  if (now - sceneStartMs >= SCENE_MS) {
    sceneIndex = (sceneIndex + 1) % SCENE_COUNT;
    sceneStartMs = now;
    SCENES[sceneIndex].init();
    Serial.println("Escena " + String(sceneIndex + 1) + "/" + String(SCENE_COUNT));
  }
  SCENES[sceneIndex].update(now);
  delay(1);
}
