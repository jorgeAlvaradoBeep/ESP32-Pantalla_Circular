#include "AppConfig.h"
#include "Config.h"

AppConfig::AppConfig(AppLogger &loggerRef) : logger(loggerRef) {}

void AppConfig::begin()
{
  preferences.begin("connectlife", false);
  config.wifiSsid = readString("wifi_ssid");
  config.wifiPassword = readString("wifi_pass");
  config.connectLifeEmail = readString("cl_email");
  config.connectLifePassword = readString("cl_pass");
  config.accessToken = readString("cl_access");
  config.refreshToken = readString("cl_refresh");
  config.uid = readString("cl_uid");
  config.deviceId = readString("cl_device");
  config.apiBaseUrl = readString("cl_base", CONNECTLIFE_DEFAULT_API_BASE_URL);
  config.tokenExpiresAtEpoch = readUInt("cl_exp", 0);
  logger.info("Configuration loaded from NVS");
}

DeviceConfig AppConfig::get() const
{
  return config;
}

void AppConfig::saveWiFi(const String &ssid, const String &password)
{
  config.wifiSsid = ssid;
  config.wifiPassword = password;
  writeString("wifi_ssid", ssid);
  writeString("wifi_pass", password);
  logger.info("WiFi configuration saved");
}

void AppConfig::saveConnectLifeCredentials(const String &email, const String &password)
{
  config.connectLifeEmail = email;
  config.connectLifePassword = password;
  writeString("cl_email", email);
  writeString("cl_pass", password);
  logger.info("ConnectLife credentials saved");
}

void AppConfig::saveConnectLifeSession(const String &accessToken,
                                       const String &refreshToken,
                                       const String &uid,
                                       const String &deviceId,
                                       uint32_t expiresAtEpoch)
{
  config.accessToken = accessToken;
  config.refreshToken = refreshToken;
  config.uid = uid;
  config.deviceId = deviceId;
  config.tokenExpiresAtEpoch = expiresAtEpoch;

  writeString("cl_access", accessToken);
  writeString("cl_refresh", refreshToken);
  writeString("cl_uid", uid);
  writeString("cl_device", deviceId);
  writeUInt("cl_exp", expiresAtEpoch);
  logger.info("ConnectLife session saved");
}

void AppConfig::saveApiBaseUrl(const String &apiBaseUrl)
{
  config.apiBaseUrl = apiBaseUrl;
  writeString("cl_base", apiBaseUrl);
  logger.info("ConnectLife API base URL saved: " + apiBaseUrl);
}

void AppConfig::saveDeviceId(const String &deviceId)
{
  config.deviceId = deviceId;
  writeString("cl_device", deviceId);
  logger.info("ConnectLife device ID saved");
}

void AppConfig::clearConnectLifeSession()
{
  config.accessToken = "";
  config.refreshToken = "";
  config.uid = "";
  config.deviceId = "";
  config.tokenExpiresAtEpoch = 0;
  preferences.remove("cl_access");
  preferences.remove("cl_refresh");
  preferences.remove("cl_uid");
  preferences.remove("cl_device");
  preferences.remove("cl_exp");
  logger.warn("ConnectLife session cleared");
}

String AppConfig::readString(const char *key, const String &fallback)
{
  return preferences.getString(key, fallback);
}

uint32_t AppConfig::readUInt(const char *key, uint32_t fallback)
{
  return preferences.getUInt(key, fallback);
}

void AppConfig::writeString(const char *key, const String &value)
{
  preferences.putString(key, value);
}

void AppConfig::writeUInt(const char *key, uint32_t value)
{
  preferences.putUInt(key, value);
}
