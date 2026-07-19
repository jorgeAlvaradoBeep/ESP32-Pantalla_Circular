#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "AppConfig.h"
#include "AppLogger.h"

enum class AcPower {
  Unknown,
  Off,
  On
};

struct AcState {
  AcPower power = AcPower::Unknown;
  int targetTemperature = 24;
  // Temperatura ambiente medida por el sensor interno del propio equipo.
  // No siempre la publica: hasAmbient dice si el dato es fiable.
  float ambientTemperature = NAN;
  bool hasAmbient = false;
  String mode = "auto";
  String fanSpeed = "auto";
  bool swing = false;
  bool sleep = false;
  bool turbo = false;
  bool online = false;
  bool authenticated = false;
  String lastError;
  unsigned long lastSyncMs = 0;
};

class ConnectLifeClient {
public:
  ConnectLifeClient(AppConfig &config, AppLogger &logger);

  void begin();
  bool login();
  bool refreshToken();
  bool discoverDevice();
  bool pollState();

  bool setPower(bool on);
  bool setTargetTemperature(int temperatureC);
  bool setMode(const String &mode);
  bool setFanSpeed(const String &fanSpeed);
  bool setSwing(bool enabled);
  bool setSleep(bool enabled);
  bool setTurbo(bool enabled);

  AcState getState() const;
  bool hasSession() const;
  bool tokenLooksExpired() const;
  String statusText() const;

private:
  AppConfig &configStore;
  AppLogger &logger;
  AcState state;

  bool ensureAuthenticated();
  bool requestJson(const char *method,
                   const String &url,
                   const JsonDocument *body,
                   JsonDocument &response,
                   const String &contentType,
                   JsonDocument *filter = nullptr,
                   bool logBody = false);
  bool requestRaw(const char *method,
                  const String &url,
                  const String &payload,
                  JsonDocument &response,
                  const String &contentType,
                  JsonDocument *filter = nullptr,
                  bool logBody = false);
  bool requestForm(const String &url, const String &formBody, JsonDocument &response);
  bool requestConnectLifeGet(const String &path, JsonDocument &response, JsonDocument *filter = nullptr);
  bool requestConnectLifePost(const String &path, JsonDocument &body, JsonDocument &response);
  bool sendCommand(const String &capability, bool value);
  bool sendCommand(const String &capability, int value);
  bool sendCommand(const String &capability, const String &value);
  bool sendCommandDocument(JsonDocument &body);
  bool parseLoginResponse(JsonDocument &doc);
  bool parseDeviceList(JsonDocument &doc);
  void parseState(JsonDocument &doc);
  String apiUrl(const String &path) const;
  String buildCommonPayload(JsonDocument &doc, bool includeAccessToken);
  String buildSignedQuery(JsonDocument &doc);
  String getSignature(JsonDocument &doc);
  String sortedPayloadForSignature(JsonDocument &doc);
  String urlEncode(const String &value) const;
  String md5Hex(const String &value) const;
  String currentTimestampMs() const;
  String modeToCode(const String &mode) const;
  String fanToCode(const String &fanSpeed) const;
  String modeFromCode(const String &code) const;
  String fanFromCode(const String &code) const;
  uint32_t nowEpoch() const;
  void recordError(const String &message);
};
