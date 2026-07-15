#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "AppConfig.h"
#include "AppLogger.h"
#include "CircularDisplay.h"
#include "Config.h"
#include "ConnectLifeClient.h"
#include "Sensor.h"
#include "WebPortal.h"
#include "secrets.example.h"

AppLogger logger;
AppConfig appConfig(logger);
Sensor sensor(DHT_PIN, DHT11, logger);
ConnectLifeClient connectLife(appConfig, logger);
WebPortal webPortal(appConfig, connectLife, sensor, logger);
CircularDisplay display;

static unsigned long lastSensorReadMs = 0;
static unsigned long lastConnectLifePollMs = 0;
static unsigned long lastWiFiReconnectMs = 0;

static void startWiFi()
{
  const DeviceConfig cfg = appConfig.get();
  WiFi.mode(WIFI_STA);

  if (cfg.wifiSsid.length() > 0) {
    logger.info("Connecting to WiFi SSID: " + cfg.wifiSsid);
    display.showWiFiConnecting(cfg.wifiSsid);
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());

    const unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS) {
      display.tick();
      delay(250);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    logger.info("WiFi connected. IP: " + WiFi.localIP().toString());
    display.showWiFiConnected(WiFi.localIP());
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    const unsigned long timeStartMs = millis();
    while (time(nullptr) < 1700000000 && millis() - timeStartMs < 5000) {
      display.tick();
      delay(100);
    }
    logger.info(time(nullptr) > 1700000000 ? "NTP time synchronized" : "NTP sync pending");
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD);
  logger.warn(String("WiFi not configured or unavailable. Setup AP: ") + DEFAULT_AP_SSID);
  logger.info("Setup AP IP: " + WiFi.softAPIP().toString());
  display.showSetupPortal(WiFi.softAPIP(), DEFAULT_AP_SSID);
}

static void keepWiFiAlive()
{
  const DeviceConfig cfg = appConfig.get();
  if (cfg.wifiSsid.length() == 0 || WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (millis() - lastWiFiReconnectMs < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }

  lastWiFiReconnectMs = millis();
  logger.warn("WiFi disconnected. Reconnecting...");
  WiFi.disconnect(false);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
}

void setup()
{
  Serial.begin(SERIAL_BAUD_RATE);
  delay(250);

  logger.begin();
  logger.info("Booting ConnectLife ESP32 controller");
  if (display.begin()) {
    display.showBoot();
  } else {
    logger.warn("Circular display did not initialize");
  }

  appConfig.begin();
  sensor.begin();
  startWiFi();

  connectLife.begin();
  webPortal.begin();
  display.showConnectLifeStatus(connectLife.statusText(), connectLife.hasSession());
}

void loop()
{
  webPortal.handleClient();
  keepWiFiAlive();

  const unsigned long now = millis();

  if (now - lastSensorReadMs >= SENSOR_READ_INTERVAL_MS) {
    lastSensorReadMs = now;
    sensor.update();
    const SensorReading reading = sensor.getReading();
    const AcState ac = connectLife.getState();
    const IPAddress ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP();
    display.showReady(ip, reading.temperatureC, ac.targetTemperature, ac.mode);
  }

  if (WiFi.status() == WL_CONNECTED && now - lastConnectLifePollMs >= CONNECTLIFE_POLL_INTERVAL_MS) {
    lastConnectLifePollMs = now;
    if (!connectLife.pollState()) {
      display.showError("ConnectLife", connectLife.statusText(), "Revisa /config");
    } else {
      display.showConnectLifeStatus(connectLife.statusText(), true);
    }
  }
}
