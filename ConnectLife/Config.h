#pragma once

#include <Arduino.h>

static const uint32_t SERIAL_BAUD_RATE = 115200;

static const char DEFAULT_AP_SSID[] = "ConnectLife-Setup";
static const char DEFAULT_AP_PASSWORD[] = "connectlife";

static const uint8_t DHT_PIN = 4;
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static const uint32_t WIFI_RECONNECT_INTERVAL_MS = 15000;
static const uint32_t SENSOR_READ_INTERVAL_MS = 5000;
static const uint32_t CONNECTLIFE_POLL_INTERVAL_MS = 15000;

static const int8_t DISPLAY_PIN_SCLK = 12;
static const int8_t DISPLAY_PIN_MOSI = 11;
static const int8_t DISPLAY_PIN_DC = 8;
static const int8_t DISPLAY_PIN_CS = 10;
static const int8_t DISPLAY_PIN_RST = 9;
static const int8_t DISPLAY_PIN_BL = 7;
static const uint32_t DISPLAY_SPI_FREQ_HZ = 40000000;

static const char CONNECTLIFE_DEFAULT_API_BASE_URL[] = "https://clife-eu-gateway.hijuconn.com";
static const uint32_t CONNECTLIFE_HTTP_TIMEOUT_MS = 12000;
static const uint32_t CONNECTLIFE_TOKEN_REFRESH_SKEW_SECONDS = 120;

static const char CONNECTLIFE_GIGYA_API_KEY[] = "4_yhTWQmHFpZkQZDSV1uV-_A";
static const char CONNECTLIFE_GIGYA_GMID[] = "gmid.ver4.AtLt3mZAMA.C8m5VqSTEQDrTRrkYYDgOaJWcyQ-XHow5nzQSXJF3EO3TnqTJ8tKUmQaaQ6z8p0s.zcTbHe6Ax6lHfvTN7JUj7VgO4x8Vl-vk1u0kZcrkKmKWw8K9r0shyut_at5Q0ri6zTewnAv2g1Dc8dauuyd-Sw.sc3";
static const char CONNECTLIFE_CLIENT_ID[] = "5065059336212";
static const char CONNECTLIFE_CLIENT_SECRET[] = "07swfKgvJhC3ydOUS9YV_SwVz0i4LKqlOLGNUukYHVMsJRF1b-iWeUGcNlXyYCeK";
static const char CONNECTLIFE_REDIRECT_URI[] = "https://api.connectlife.io/swagger/oauth2-redirect.html";
static const char CONNECTLIFE_APP_ID[] = "47110565134383";
static const char CONNECTLIFE_APP_SECRET[] = "yOzhz6junYno-nmULM3Wr7PU_dpSZN22ZdluvVWZ4uW5ZwwG8fIGCHTbrhcnU-iv";
static const char CONNECTLIFE_SIGNATURE_SALT[] = "D9519A4B756946F081B7BB5B5E8D1197";
static const char CONNECTLIFE_PUBLIC_KEY[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAyyWrNG6q475HIHu7sMVu\n"
    "vHof6vlgPeixmxa4EL/UsvVvHPz33NnWoQetQqit9TBNzUjMXw0KlY9PXM4iqHUU\n"
    "U+dSyNDq1jZWIiJ2C2FccppswJtIKL3NRMFvT9PFh6NlP/4FUcQKojgKFbF7Kacc\n"
    "JPKYHlwaO7qgoIjLxAHlSOXGpucJcOkPzT2EqsSVnW8sn8kenvNmghXDayhgxsh6\n"
    "AyxK4kehJplEnmX/iYCfNoFXknGcLqFWYccgBz3fybvx30C/0IgU1980L8QsUAv5\n"
    "esZmN8ugnbRgLRxKRlkQQLxQAiZMZdKTAx665YflT3YMHJvEFE8c2XFgoxHzSMc4\n"
    "BwIDAQAB\n"
    "-----END PUBLIC KEY-----\n";
