#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

class CircularDisplay {
public:
  CircularDisplay();

  bool begin();
  bool isReady() const;

  void showBoot();
  void showWiFiConnecting(const String &ssid);
  void showWiFiConnected(const IPAddress &ip);
  void showSetupPortal(const IPAddress &ip, const String &ssid);
  void showConnectLifeStatus(const String &status, bool ok);
  void showReady(const IPAddress &ip, float ambientTemp, int targetTemp, const String &mode);
  void showError(const String &title, const String &detail, const String &hint);
  void tick();

private:
  Arduino_DataBus *bus;
  Arduino_GFX *gfx;
  bool ready;
  uint32_t lastSpinnerMs;
  int16_t spinnerAngle;

  void printCentered(const String &text, int16_t y, uint8_t size, uint16_t color);
  void drawBase(const String &title, const String &subtitle, uint16_t accent);
  void drawStatusLine(const String &text, uint16_t color);
  void ringArc(float startDeg, float sweepDeg, uint16_t color);
  void drawSpinner();
  String fitText(const String &text, uint8_t maxChars) const;
};
