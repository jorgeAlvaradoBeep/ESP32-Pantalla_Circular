#include "CircularDisplay.h"

#include "Config.h"
#include <math.h>

static const uint16_t COL_BG = RGB565_BLACK;
static const uint16_t COL_TITLE = 0x07FF;
static const uint16_t COL_TEXT = RGB565_WHITE;
static const uint16_t COL_MUTED = 0x8410;
static const uint16_t COL_RING_B = 0x2124;
static const uint16_t COL_RING_F = 0x07E0;
static const uint16_t COL_WARN = 0xFD20;
static const uint16_t COL_ERROR = 0xF800;
static const int16_t SCR_W = 240;
static const int16_t SCR_H = 240;
static const int16_t CX = SCR_W / 2;
static const int16_t CY = SCR_H / 2;

CircularDisplay::CircularDisplay()
    : bus(new Arduino_ESP32SPI(DISPLAY_PIN_DC,
                               DISPLAY_PIN_CS,
                               DISPLAY_PIN_SCLK,
                               DISPLAY_PIN_MOSI,
                               GFX_NOT_DEFINED)),
      gfx(new Arduino_GC9A01(bus, DISPLAY_PIN_RST, 0, true)),
      ready(false),
      lastSpinnerMs(0),
      spinnerAngle(0) {}

bool CircularDisplay::begin()
{
  if (DISPLAY_PIN_BL >= 0) {
    pinMode(DISPLAY_PIN_BL, OUTPUT);
    digitalWrite(DISPLAY_PIN_BL, HIGH);
  }

  ready = gfx->begin(DISPLAY_SPI_FREQ_HZ);
  if (ready) {
    gfx->fillScreen(COL_BG);
  }
  return ready;
}

bool CircularDisplay::isReady() const
{
  return ready;
}

void CircularDisplay::showBoot()
{
  drawBase("ConnectLife", "Iniciando ESP32", COL_TITLE);
  drawStatusLine("Preparando servicios", COL_TEXT);
}

void CircularDisplay::showWiFiConnecting(const String &ssid)
{
  drawBase("WiFi", "Conectando", COL_TITLE);
  drawStatusLine(fitText(ssid, 21), COL_TEXT);
}

void CircularDisplay::showWiFiConnected(const IPAddress &ip)
{
  drawBase("WiFi OK", ip.toString(), COL_RING_F);
  drawStatusLine("Abre /config o /", COL_TEXT);
}

void CircularDisplay::showSetupPortal(const IPAddress &ip, const String &ssid)
{
  drawBase("Configurar", fitText(ssid, 18), COL_WARN);
  drawStatusLine(ip.toString() + "/config", COL_TEXT);
}

void CircularDisplay::showConnectLifeStatus(const String &status, bool ok)
{
  drawBase("ConnectLife", ok ? "Sincronizado" : "Revisar estado", ok ? COL_RING_F : COL_WARN);
  drawStatusLine(fitText(status, 22), ok ? COL_TEXT : COL_WARN);
}

void CircularDisplay::showReady(const IPAddress &ip,
                                float ambientTemp,
                                float acAmbientTemp,
                                int targetTemp,
                                const String &mode,
                                const String &controlLine)
{
  if (!ready) return;
  gfx->fillScreen(COL_BG);
  ringArc(0, 360, COL_RING_B);
  ringArc(270, 280, COL_RING_F);

  printCentered("ConnectLife", 44, 2, COL_TITLE);

  char temp[18];
  if (isnan(ambientTemp)) {
    snprintf(temp, sizeof(temp), "-- C");
  } else {
    snprintf(temp, sizeof(temp), "%.1f C", ambientTemp);
  }
  printCentered(temp, 72, 3, COL_TEXT);

  char acTemp[24];
  if (isnan(acAmbientTemp)) {
    snprintf(acTemp, sizeof(acTemp), "Aire -- C");
  } else {
    snprintf(acTemp, sizeof(acTemp), "Aire %.1f C", acAmbientTemp);
  }
  printCentered(acTemp, 104, 1, COL_MUTED);

  printCentered("Objetivo " + String(targetTemp) + " C", 118, 1, COL_TEXT);
  printCentered("Modo " + mode, 132, 1, COL_MUTED);
  printCentered(fitText(controlLine, 22), 150, 1, COL_RING_F);
  printCentered(ip.toString(), 170, 1, COL_MUTED);
}

void CircularDisplay::showError(const String &title, const String &detail, const String &hint)
{
  drawBase(fitText(title, 13), fitText(detail, 22), COL_ERROR);
  drawStatusLine(fitText(hint, 22), COL_WARN);
}

void CircularDisplay::tick()
{
  drawSpinner();
}

void CircularDisplay::printCentered(const String &text, int16_t y, uint8_t size, uint16_t color)
{
  if (!ready) return;
  int16_t bx, by;
  uint16_t bw, bh;
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->getTextBounds(text.c_str(), 0, 0, &bx, &by, &bw, &bh);
  gfx->setCursor(CX - static_cast<int16_t>(bw) / 2 - bx, y);
  gfx->print(text.c_str());
}

void CircularDisplay::drawBase(const String &title, const String &subtitle, uint16_t accent)
{
  if (!ready) return;
  gfx->fillScreen(COL_BG);
  ringArc(0, 360, COL_RING_B);
  ringArc(270, 76, accent);
  printCentered(title, 78, 2, accent);
  printCentered(subtitle, 116, 1, COL_TEXT);
}

void CircularDisplay::drawStatusLine(const String &text, uint16_t color)
{
  if (!ready) return;
  gfx->fillRect(24, 148, 192, 36, COL_BG);
  printCentered(text, 158, 1, color);
}

void CircularDisplay::ringArc(float startDeg, float sweepDeg, uint16_t color)
{
  if (!ready) return;
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

void CircularDisplay::drawSpinner()
{
  if (!ready || millis() - lastSpinnerMs < 90) {
    return;
  }
  lastSpinnerMs = millis();
  ringArc(0, 360, COL_RING_B);
  ringArc(spinnerAngle, 60, COL_RING_F);
  spinnerAngle = (spinnerAngle + 18) % 360;
}

String CircularDisplay::fitText(const String &text, uint8_t maxChars) const
{
  if (text.length() <= maxChars) {
    return text;
  }
  if (maxChars <= 3) {
    return text.substring(0, maxChars);
  }
  return text.substring(0, maxChars - 3) + "...";
}
