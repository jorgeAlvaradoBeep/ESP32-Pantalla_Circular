#include "AppLogger.h"

void AppLogger::begin()
{
  info("Logger ready");
}

void AppLogger::info(const String &message)
{
  append("INFO", message);
}

void AppLogger::warn(const String &message)
{
  append("WARN", message);
}

void AppLogger::error(const String &message)
{
  append("ERROR", message);
}

String AppLogger::asText() const
{
  String out;
  const uint8_t count = wrapped ? MaxEntries : nextIndex;
  for (uint8_t i = 0; i < count; i++) {
    const uint8_t index = wrapped ? (nextIndex + i) % MaxEntries : i;
    out += entries[index];
    out += '\n';
  }
  return out;
}

void AppLogger::append(const String &level, const String &message)
{
  String line = "[" + String(millis() / 1000) + "s] " + level + " " + message;
  Serial.println(line);
  entries[nextIndex] = line;
  nextIndex = (nextIndex + 1) % MaxEntries;
  if (nextIndex == 0) {
    wrapped = true;
  }
}
