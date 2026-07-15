#include "Sensor.h"

Sensor::Sensor(uint8_t pin, uint8_t type, AppLogger &loggerRef)
    : dht(pin, type), logger(loggerRef) {}

void Sensor::begin()
{
  dht.begin();
  logger.info("DHT sensor initialized");
}

bool Sensor::update()
{
  const float humidity = dht.readHumidity();
  const float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    reading.valid = false;
    logger.warn("DHT read failed");
    return false;
  }

  reading.temperatureC = temperature;
  reading.humidity = humidity;
  reading.valid = true;
  reading.updatedAtMs = millis();
  return true;
}

SensorReading Sensor::getReading() const
{
  return reading;
}
