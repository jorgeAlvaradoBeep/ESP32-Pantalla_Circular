#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <WebServer.h>

#include "AppConfig.h"
#include "AppLogger.h"
#include "ConnectLifeClient.h"
#include "Sensor.h"

class WebPortal {
public:
  WebPortal(AppConfig &config,
            ConnectLifeClient &connectLife,
            Sensor &sensor,
            AppLogger &logger);

  void begin();
  void handleClient();

private:
  WebServer server;
  AppConfig &config;
  ConnectLifeClient &connectLife;
  Sensor &sensor;
  AppLogger &logger;

  void registerRoutes();
  void sendIndex();
  void sendConfig();
  void redirectToConfig();
  void sendState();
  void saveConfig();
  void loginConnectLife();
  void restartEsp();
  void sendLogs();
  void handleAcCommand();
  void sendOtaForm();
  void handleOtaUpload();
  void finishOta();

  bool readJsonBody(JsonDocument &doc);
  void sendJson(JsonDocument &doc, int status = 200);
  void sendOk(bool ok, const String &message);
};
