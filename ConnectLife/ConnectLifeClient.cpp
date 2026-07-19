#include "ConnectLifeClient.h"
#include "Config.h"

#include <MD5Builder.h>
#include <time.h>
#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

ConnectLifeClient::ConnectLifeClient(AppConfig &config, AppLogger &loggerRef)
    : configStore(config), logger(loggerRef) {}

void ConnectLifeClient::begin()
{
  state.authenticated = hasSession();
  logger.info("ConnectLife Hijuconn client ready");
}

bool ConnectLifeClient::login()
{
  const DeviceConfig cfg = configStore.get();
  if (cfg.connectLifeEmail.length() == 0 || cfg.connectLifePassword.length() == 0) {
    recordError("ConnectLife credentials are missing");
    return false;
  }

  DynamicJsonDocument response(8192);
  String form = "loginID=" + urlEncode(cfg.connectLifeEmail) +
                "&password=" + urlEncode(cfg.connectLifePassword) +
                "&APIKey=" + urlEncode(CONNECTLIFE_GIGYA_API_KEY) +
                "&gmid=" + urlEncode(CONNECTLIFE_GIGYA_GMID);

  if (!requestForm("https://accounts.eu1.gigya.com/accounts.login", form, response)) {
    return false;
  }

  const String loginToken = response["sessionInfo"]["cookieValue"].as<String>();
  const String uid = response["UID"].as<String>();
  if (loginToken.length() == 0 || uid.length() == 0) {
    recordError("Gigya login did not return session token");
    return false;
  }

  response.clear();
  form = "APIKey=" + urlEncode(CONNECTLIFE_GIGYA_API_KEY) +
         "&gmid=" + urlEncode(CONNECTLIFE_GIGYA_GMID) +
         "&login_token=" + urlEncode(loginToken);
  if (!requestForm("https://accounts.eu1.gigya.com/accounts.getJWT", form, response)) {
    return false;
  }

  const String idToken = response["id_token"].as<String>();
  if (idToken.length() == 0) {
    recordError("Gigya getJWT did not return id_token");
    return false;
  }

  StaticJsonDocument<768> authorizeBody;
  authorizeBody["client_id"] = CONNECTLIFE_CLIENT_ID;
  authorizeBody["idToken"] = idToken;
  authorizeBody["response_type"] = "code";
  authorizeBody["redirect_uri"] = CONNECTLIFE_REDIRECT_URI;
  authorizeBody["thirdType"] = "CDC";
  authorizeBody["thirdClientId"] = uid;

  response.clear();
  if (!requestJson("POST", "https://oauth.hijuconn.com/oauth/authorize", &authorizeBody, response, "application/json")) {
    return false;
  }

  const String code = response["code"].as<String>();
  if (code.length() == 0) {
    recordError("OAuth authorize did not return code");
    return false;
  }

  response.clear();
  form = "client_id=" + urlEncode(CONNECTLIFE_CLIENT_ID) +
         "&code=" + urlEncode(code) +
         "&grant_type=authorization_code" +
         "&client_secret=" + urlEncode(CONNECTLIFE_CLIENT_SECRET) +
         "&redirect_uri=" + urlEncode(CONNECTLIFE_REDIRECT_URI);

  if (!requestForm("https://oauth.hijuconn.com/oauth/token", form, response)) {
    return false;
  }

  if (!parseLoginResponse(response)) {
    recordError("OAuth token response did not include access token");
    return false;
  }

  DeviceConfig updated = configStore.get();
  configStore.saveConnectLifeSession(updated.accessToken, updated.refreshToken, uid, updated.deviceId, updated.tokenExpiresAtEpoch);
  state.authenticated = true;
  logger.info("ConnectLife login succeeded");
  discoverDevice();
  return true;
}

bool ConnectLifeClient::refreshToken()
{
  const DeviceConfig cfg = configStore.get();
  if (cfg.refreshToken.length() == 0) {
    return login();
  }

  DynamicJsonDocument response(4096);
  const String form = "client_id=" + urlEncode(CONNECTLIFE_CLIENT_ID) +
                      "&grant_type=refresh_token" +
                      "&refresh_token=" + urlEncode(cfg.refreshToken) +
                      "&client_secret=" + urlEncode(CONNECTLIFE_CLIENT_SECRET);

  if (!requestForm("https://oauth.hijuconn.com/oauth/token", form, response)) {
    logger.warn("Refresh token failed, falling back to full login");
    return login();
  }

  if (!parseLoginResponse(response)) {
    recordError("Refresh response did not include access token");
    return false;
  }

  state.authenticated = true;
  logger.info("ConnectLife token refreshed");
  return true;
}

bool ConnectLifeClient::discoverDevice()
{
  if (!ensureAuthenticated()) {
    return false;
  }

  StaticJsonDocument<768> filter;
  const char *deviceKeys[] = {"puid", "deviceNickName", "deviceTypeCode", "deviceFeatureCode", "offlineState"};
  for (const char *key : deviceKeys) {
    filter["response"]["deviceList"][0][key] = true;
    filter["deviceList"][0][key] = true;
  }

  DynamicJsonDocument response(8192);
  if (!requestConnectLifeGet("/clife-svc/pu/get_device_status_list", response, &filter)) {
    return false;
  }

  return parseDeviceList(response);
}

bool ConnectLifeClient::pollState()
{
  if (!ensureAuthenticated()) {
    return false;
  }

  DeviceConfig cfg = configStore.get();
  if (cfg.deviceId.length() == 0 && !discoverDevice()) {
    recordError("No ConnectLife device ID available");
    return false;
  }

  StaticJsonDocument<1024> filter;
  const char *pollDeviceKeys[] = {"puid", "offlineState"};
  // Nota: t_temp es el SETPOINT del equipo, no lo que mide. La temperatura
  // ambiente interna se publica con un nombre que varía entre modelos, así que
  // pedimos todos los candidatos conocidos y en parseState() usamos el primero
  // que venga. Si tu equipo usa otro nombre, vuélcalo con tools/connectlife_probe.py
  // y añádelo aquí y en AMBIENT_KEYS.
  const char *statusKeys[] = {"t_power", "t_temp", "t_work_mode", "t_fan_speed",
                              "t_sleep", "t_super", "t_swing_direction", "t_swing_angle",
                              "f_temp_in", "f_temp_indoor", "t_temp_in", "f_temp_current"};
  for (const char *key : pollDeviceKeys) {
    filter["response"]["deviceList"][0][key] = true;
    filter["deviceList"][0][key] = true;
  }
  for (const char *key : statusKeys) {
    filter["response"]["deviceList"][0]["statusList"][key] = true;
    filter["deviceList"][0]["statusList"][key] = true;
  }

  DynamicJsonDocument response(8192);
  if (!requestConnectLifeGet("/clife-svc/pu/get_device_status_list", response, &filter)) {
    return false;
  }

  parseState(response);
  state.online = state.power != AcPower::Unknown;
  state.lastSyncMs = millis();
  if (state.online) {
    state.lastError = "";
  }
  return state.online;
}

bool ConnectLifeClient::setPower(bool on)
{
  return sendCommand("t_power", on ? 1 : 0);
}

bool ConnectLifeClient::setTargetTemperature(int temperatureC)
{
  StaticJsonDocument<384> body;
  body["t_temp"] = temperatureC;
  body["t_temp_type"] = "1";
  return sendCommandDocument(body);
}

bool ConnectLifeClient::setMode(const String &mode)
{
  if (mode == "off") {
    return setPower(false);
  }
  StaticJsonDocument<384> body;
  body["t_power"] = 1;
  body["t_work_mode"] = modeToCode(mode).toInt();
  return sendCommandDocument(body);
}

bool ConnectLifeClient::setFanSpeed(const String &fanSpeed)
{
  return sendCommand("t_fan_speed", static_cast<int>(fanToCode(fanSpeed).toInt()));
}

bool ConnectLifeClient::setSwing(bool enabled)
{
  StaticJsonDocument<384> body;
  body["t_swing_direction"] = enabled ? 3 : 0;
  body["t_swing_angle"] = enabled ? 0 : 2;
  return sendCommandDocument(body);
}

bool ConnectLifeClient::setSleep(bool enabled)
{
  return sendCommand("t_sleep", enabled ? 1 : 0);
}

bool ConnectLifeClient::setTurbo(bool enabled)
{
  return sendCommand("t_super", enabled ? 1 : 0);
}

AcState ConnectLifeClient::getState() const
{
  return state;
}

bool ConnectLifeClient::hasSession() const
{
  const DeviceConfig cfg = configStore.get();
  return cfg.accessToken.length() > 0 || cfg.refreshToken.length() > 0;
}

bool ConnectLifeClient::tokenLooksExpired() const
{
  const DeviceConfig cfg = configStore.get();
  if (cfg.tokenExpiresAtEpoch == 0) {
    return false;
  }
  return nowEpoch() + CONNECTLIFE_TOKEN_REFRESH_SKEW_SECONDS >= cfg.tokenExpiresAtEpoch;
}

String ConnectLifeClient::statusText() const
{
  if (state.lastError.length() > 0) {
    return "Error: " + state.lastError;
  }
  if (!state.authenticated) {
    return "Sin autenticar";
  }
  return state.online ? "En línea" : "Autenticado";
}

bool ConnectLifeClient::ensureAuthenticated()
{
  if (!hasSession()) {
    return login();
  }
  if (tokenLooksExpired()) {
    return refreshToken();
  }
  state.authenticated = true;
  return true;
}

bool ConnectLifeClient::requestJson(const char *method,
                                    const String &url,
                                    const JsonDocument *body,
                                    JsonDocument &response,
                                    const String &contentType,
                                    JsonDocument *filter,
                                    bool logBody)
{
  String payload;
  if (body != nullptr) {
    serializeJson(*body, payload);
  }
  return requestRaw(method, url, payload, response, contentType, filter, logBody);
}

bool ConnectLifeClient::requestRaw(const char *method,
                                   const String &url,
                                   const String &payload,
                                   JsonDocument &response,
                                   const String &contentType,
                                   JsonDocument *filter,
                                   bool logBody)
{
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    recordError("HTTP begin failed");
    return false;
  }

  http.setTimeout(CONNECTLIFE_HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", contentType);
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "Runner/2.0.6 (iPhone; iOS 17.2.1; Scale/3.00)");

  int status = 0;
  if (String(method) == "GET") {
    status = http.GET();
  } else if (String(method) == "POST") {
    status = http.POST(payload);
  } else {
    http.end();
    recordError("Unsupported HTTP method");
    return false;
  }

  const String responseBody = http.getString();
  http.end();

  if (logBody) {
    Serial.println("---- ConnectLife " + String(method) + " " + url);
    Serial.println("---- HTTP " + String(status) + ", " + String(responseBody.length()) + " bytes:");
    Serial.println(responseBody);
    Serial.println("----");
  }

  if (status < 200 || status >= 300) {
    recordError("HTTP " + String(status) + " from " + url + " body: " + responseBody.substring(0, 200));
    return false;
  }

  if (responseBody.length() == 0) {
    return true;
  }

  DeserializationError error = filter != nullptr
      ? deserializeJson(response, responseBody, DeserializationOption::Filter(*filter))
      : deserializeJson(response, responseBody);
  if (error) {
    recordError("JSON parse failed: " + String(error.c_str()) + " (body " + String(responseBody.length()) + " bytes)");
    return false;
  }

  return true;
}

bool ConnectLifeClient::requestForm(const String &url, const String &formBody, JsonDocument &response)
{
  return requestRaw("POST", url, formBody, response, "application/x-www-form-urlencoded");
}

bool ConnectLifeClient::requestConnectLifeGet(const String &path, JsonDocument &response, JsonDocument *filter)
{
  DynamicJsonDocument payload(6144);
  buildCommonPayload(payload, true);
  const String query = buildSignedQuery(payload);
  if (payload.overflowed()) {
    recordError("Signed GET payload overflowed its JsonDocument");
    return false;
  }
  if (payload["sign"].as<String>().length() == 0) {
    recordError("Signature missing from GET request");
    return false;
  }
  return requestJson("GET", apiUrl(path) + "?" + query, nullptr, response, "application/json", filter, true);
}

bool ConnectLifeClient::requestConnectLifePost(const String &path, JsonDocument &body, JsonDocument &response)
{
  buildCommonPayload(body, true);
  body["sign"] = getSignature(body);
  if (body.overflowed()) {
    recordError("Signed POST payload overflowed its JsonDocument");
    return false;
  }
  if (body["sign"].as<String>().length() == 0) {
    recordError("Signature missing from POST request");
    return false;
  }
  return requestJson("POST", apiUrl(path), &body, response, "application/json", nullptr, true);
}

bool ConnectLifeClient::sendCommand(const String &capability, bool value)
{
  StaticJsonDocument<384> body;
  body[capability] = value ? 1 : 0;
  return sendCommandDocument(body);
}

bool ConnectLifeClient::sendCommand(const String &capability, int value)
{
  StaticJsonDocument<384> body;
  body[capability] = value;
  return sendCommandDocument(body);
}

bool ConnectLifeClient::sendCommand(const String &capability, const String &value)
{
  StaticJsonDocument<384> body;
  body[capability] = value;
  return sendCommandDocument(body);
}

bool ConnectLifeClient::sendCommandDocument(JsonDocument &properties)
{
  if (!ensureAuthenticated()) {
    return false;
  }

  DeviceConfig cfg = configStore.get();
  if (cfg.deviceId.length() == 0 && !discoverDevice()) {
    return false;
  }

  cfg = configStore.get();
  DynamicJsonDocument body(6144);
  body["puid"] = cfg.deviceId;
  JsonObject props = body.createNestedObject("properties");
  for (JsonPair kv : properties.as<JsonObject>()) {
    props[kv.key()] = kv.value();
  }

  DynamicJsonDocument response(4096);
  const bool ok = requestConnectLifePost("/device/pu/property/set", body, response);
  if (ok) {
    logger.info("ConnectLife command sent");
    pollState();
  }
  return ok;
}

bool ConnectLifeClient::parseLoginResponse(JsonDocument &doc)
{
  const String access = doc["access_token"].as<String>();
  String refresh = doc["refresh_token"].as<String>();
  if (refresh.length() == 0) {
    refresh = configStore.get().refreshToken;
  }

  uint32_t expiresIn = doc["expires_in"] | 1440;
  if (expiresIn < 10000) {
    expiresIn *= 60;
  }

  if (access.length() == 0) {
    return false;
  }

  const DeviceConfig oldCfg = configStore.get();
  configStore.saveConnectLifeSession(access, refresh, oldCfg.uid, oldCfg.deviceId, nowEpoch() + expiresIn);
  return true;
}

bool ConnectLifeClient::parseDeviceList(JsonDocument &doc)
{
  JsonArray devices = doc["response"]["deviceList"].as<JsonArray>();
  if (devices.isNull()) {
    devices = doc["deviceList"].as<JsonArray>();
  }

  if (devices.isNull()) {
    recordError("Server response has no deviceList (raw body printed on serial: likely bad sign/token/timeStamp)");
    return false;
  }

  if (devices.size() == 0) {
    recordError("Account has 0 devices on this gateway. Check account region / API base URL");
    return false;
  }

  String fallbackPuid;
  String fallbackDetail;
  for (JsonObject device : devices) {
    const String puid = device["puid"].as<String>();
    const String name = device["deviceNickName"].as<String>();
    const String typeCode = device["deviceTypeCode"].as<String>();
    const int offlineState = device["offlineState"] | 0;
    logger.info("Device: puid=" + puid + " name=" + name + " type=" + typeCode +
                " online=" + String(offlineState));
    if (puid.length() == 0) {
      continue;
    }
    if (offlineState == 1 &&
        (typeCode == "009" || typeCode == "006" || typeCode == "008")) {
      configStore.saveDeviceId(puid);
      logger.info("ConnectLife AC discovered: " + puid);
      return true;
    }
    if (fallbackPuid.length() == 0) {
      fallbackPuid = puid;
      fallbackDetail = "type=" + typeCode + " online=" + String(offlineState);
    }
  }

  if (fallbackPuid.length() > 0) {
    logger.warn("No device matched AC filter (type 009/006/008 + online). Falling back to first device: " +
                fallbackPuid + " (" + fallbackDetail + ")");
    configStore.saveDeviceId(fallbackPuid);
    return true;
  }

  recordError("Device list had entries but none with a puid");
  return false;
}

void ConnectLifeClient::parseState(JsonDocument &doc)
{
  const DeviceConfig cfg = configStore.get();
  JsonArray devices = doc["response"]["deviceList"].as<JsonArray>();
  if (devices.isNull()) {
    devices = doc["deviceList"].as<JsonArray>();
  }

  for (JsonObject device : devices) {
    const String puid = device["puid"].as<String>();
    if (puid != cfg.deviceId) {
      continue;
    }

    JsonObject status = device["statusList"].as<JsonObject>();
    const String power = status["t_power"].as<String>();
    state.power = power == "1" ? AcPower::On : AcPower::Off;
    const int parsedTarget = status["t_temp"].as<String>().toInt();
    if (parsedTarget > 0) {
      state.targetTemperature = parsedTarget;
    }
    state.mode = state.power == AcPower::Off ? "off" : modeFromCode(status["t_work_mode"].as<String>());
    state.fanSpeed = fanFromCode(status["t_fan_speed"].as<String>());
    state.sleep = status["t_sleep"].as<String>() == "1";
    state.turbo = status["t_super"].as<String>() == "1";
    state.swing = status["t_swing_direction"].as<String>() == "3" || status["t_swing_angle"].as<String>() == "0";

    // Primer candidato presente y con valor plausible gana.
    static const char *AMBIENT_KEYS[] = {"f_temp_in", "f_temp_indoor", "t_temp_in", "f_temp_current"};
    state.hasAmbient = false;
    state.ambientTemperature = NAN;
    for (const char *key : AMBIENT_KEYS) {
      if (!status.containsKey(key)) {
        continue;
      }
      const float value = status[key].as<String>().toFloat();
      if (value > -20.0f && value < 70.0f) {
        state.ambientTemperature = value;
        state.hasAmbient = true;
        break;
      }
    }
    return;
  }

  state.power = AcPower::Unknown;
  String available;
  for (JsonObject device : devices) {
    if (available.length() > 0) {
      available += ", ";
    }
    available += device["puid"].as<String>();
  }
  recordError("Configured AC " + cfg.deviceId + " not in status list. Available: [" + available + "]");
}

String ConnectLifeClient::apiUrl(const String &path) const
{
  DeviceConfig cfg = configStore.get();
  String base = cfg.apiBaseUrl.length() > 0 ? cfg.apiBaseUrl : CONNECTLIFE_DEFAULT_API_BASE_URL;
  if (base.endsWith("/")) {
    base.remove(base.length() - 1);
  }
  return base + path;
}

String ConnectLifeClient::buildCommonPayload(JsonDocument &doc, bool includeAccessToken)
{
  const String ts = currentTimestampMs();
  if (ts.length() < 13) {
    logger.warn("NTP not synced yet; timeStamp " + ts + " will likely be rejected by the server");
  }
  doc["appId"] = CONNECTLIFE_APP_ID;
  doc["appSecret"] = CONNECTLIFE_APP_SECRET;
  doc["languageId"] = "12";
  doc["randStr"] = md5Hex(ts);
  doc["timeStamp"] = ts;
  doc["timezone"] = "1.0";
  doc["version"] = "5.0";
  if (includeAccessToken) {
    doc["accessToken"] = configStore.get().accessToken;
  }
  return ts;
}

String ConnectLifeClient::buildSignedQuery(JsonDocument &doc)
{
  doc["sign"] = getSignature(doc);
  String query;
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (query.length() > 0) {
      query += "&";
    }
    query += urlEncode(String(kv.key().c_str())) + "=" + urlEncode(kv.value().as<String>());
  }
  return query;
}

String ConnectLifeClient::getSignature(JsonDocument &doc)
{
  const String toHash = sortedPayloadForSignature(doc) + CONNECTLIFE_SIGNATURE_SALT;

  unsigned char sha[32];
  mbedtls_sha256_context shaCtx;
  mbedtls_sha256_init(&shaCtx);
  mbedtls_sha256_starts(&shaCtx, 0);
  mbedtls_sha256_update(&shaCtx, reinterpret_cast<const unsigned char *>(toHash.c_str()), toHash.length());
  mbedtls_sha256_finish(&shaCtx, sha);
  mbedtls_sha256_free(&shaCtx);

  mbedtls_pk_context pk;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctrDrbg;
  mbedtls_pk_init(&pk);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctrDrbg);

  const char *personalization = "connectlife";
  if (mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy,
                            reinterpret_cast<const unsigned char *>(personalization),
                            strlen(personalization)) != 0) {
    recordError("RSA RNG seed failed");
    return "";
  }

  if (mbedtls_pk_parse_public_key(&pk,
                                  reinterpret_cast<const unsigned char *>(CONNECTLIFE_PUBLIC_KEY),
                                  strlen(CONNECTLIFE_PUBLIC_KEY) + 1) != 0) {
    recordError("RSA public key parse failed");
    return "";
  }

  unsigned char encrypted[256];
  size_t encryptedLen = 0;
  int rc = mbedtls_pk_encrypt(&pk, sha, sizeof(sha), encrypted, &encryptedLen, sizeof(encrypted),
                              mbedtls_ctr_drbg_random, &ctrDrbg);
  mbedtls_pk_free(&pk);
  mbedtls_ctr_drbg_free(&ctrDrbg);
  mbedtls_entropy_free(&entropy);

  if (rc != 0) {
    recordError("RSA signature encryption failed");
    return "";
  }

  unsigned char encoded[384];
  size_t encodedLen = 0;
  if (mbedtls_base64_encode(encoded, sizeof(encoded), &encodedLen, encrypted, encryptedLen) != 0) {
    recordError("Base64 signature failed");
    return "";
  }
  encoded[encodedLen] = '\0';
  return String(reinterpret_cast<char *>(encoded));
}

String ConnectLifeClient::sortedPayloadForSignature(JsonDocument &doc)
{
  const char *keys[24];
  uint8_t count = 0;
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (String(kv.key().c_str()) == "sign") {
      continue;
    }
    keys[count++] = kv.key().c_str();
  }

  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t j = i + 1; j < count; j++) {
      if (strcmp(keys[j], keys[i]) < 0) {
        const char *tmp = keys[i];
        keys[i] = keys[j];
        keys[j] = tmp;
      }
    }
  }

  String out;
  for (uint8_t i = 0; i < count; i++) {
    if (i > 0) {
      out += "&";
    }
    out += keys[i];
    out += "=";
    JsonVariant value = doc[keys[i]];
    if (value.is<JsonObject>() || value.is<JsonArray>()) {
      String encodedJson;
      serializeJson(value, encodedJson);
      out += encodedJson;
    } else {
      out += value.as<String>();
    }
  }
  return out;
}

String ConnectLifeClient::urlEncode(const String &value) const
{
  String encoded;
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); i++) {
    const uint8_t c = static_cast<uint8_t>(value.charAt(i));
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

String ConnectLifeClient::md5Hex(const String &value) const
{
  MD5Builder md5;
  md5.begin();
  md5.add(value);
  md5.calculate();
  return md5.toString();
}

String ConnectLifeClient::currentTimestampMs() const
{
  const time_t now = time(nullptr);
  char buffer[24];
  if (now > 1700000000) {
    snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(now) * 1000ULL + (millis() % 1000));
    return String(buffer);
  }
  snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(millis()));
  return String(buffer);
}

String ConnectLifeClient::modeToCode(const String &mode) const
{
  if (mode == "heat") return "1";
  if (mode == "cool") return "2";
  if (mode == "dry") return "3";
  if (mode == "auto") return "4";
  return "0";
}

String ConnectLifeClient::fanToCode(const String &fanSpeed) const
{
  if (fanSpeed == "low") return "6";
  if (fanSpeed == "medium") return "7";
  if (fanSpeed == "high") return "8";
  return "0";
}

String ConnectLifeClient::modeFromCode(const String &code) const
{
  if (code == "1") return "heat";
  if (code == "2") return "cool";
  if (code == "3") return "dry";
  if (code == "4") return "auto";
  return "fan";
}

String ConnectLifeClient::fanFromCode(const String &code) const
{
  if (code == "6") return "low";
  if (code == "7") return "medium";
  if (code == "8" || code == "9") return "high";
  return "auto";
}

uint32_t ConnectLifeClient::nowEpoch() const
{
  const time_t now = time(nullptr);
  if (now > 1700000000) {
    return static_cast<uint32_t>(now);
  }
  return millis() / 1000;
}

void ConnectLifeClient::recordError(const String &message)
{
  state.lastError = message;
  state.online = false;
  logger.error("ConnectLife: " + message);
}
