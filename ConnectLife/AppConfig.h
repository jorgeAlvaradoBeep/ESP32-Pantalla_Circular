#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "AppLogger.h"
#include "ControlTypes.h"

struct DeviceConfig {
  String wifiSsid;
  String wifiPassword;
  String connectLifeEmail;
  String connectLifePassword;
  String accessToken;
  String refreshToken;
  String uid;
  String deviceId;
  String apiBaseUrl;
  String timezone;
  uint32_t tokenExpiresAtEpoch;
};

class AppConfig {
public:
  explicit AppConfig(AppLogger &logger);

  void begin();
  DeviceConfig get() const;
  void saveWiFi(const String &ssid, const String &password);
  void saveConnectLifeCredentials(const String &email, const String &password);
  void saveConnectLifeSession(const String &accessToken,
                              const String &refreshToken,
                              const String &uid,
                              const String &deviceId,
                              uint32_t expiresAtEpoch);
  void saveApiBaseUrl(const String &apiBaseUrl);
  void saveDeviceId(const String &deviceId);
  void saveTimezone(const String &timezone);
  void clearConnectLifeSession();

  const ControlConfig &getControl() const;
  void saveControl(const ControlConfig &control);

private:
  Preferences preferences;
  DeviceConfig config;
  ControlConfig control;
  AppLogger &logger;

  void loadControl();

  String readString(const char *key, const String &fallback = "");
  uint32_t readUInt(const char *key, uint32_t fallback = 0);
  void writeString(const char *key, const String &value);
  void writeUInt(const char *key, uint32_t value);
};
