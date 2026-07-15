#pragma once

#include <Arduino.h>
#include <DHT.h>

#include "AppLogger.h"

struct SensorReading {
  float temperatureC = NAN;
  float humidity = NAN;
  bool valid = false;
  unsigned long updatedAtMs = 0;
};

class Sensor {
public:
  Sensor(uint8_t pin, uint8_t type, AppLogger &logger);

  void begin();
  bool update();
  SensorReading getReading() const;

private:
  DHT dht;
  AppLogger &logger;
  SensorReading reading;
};
