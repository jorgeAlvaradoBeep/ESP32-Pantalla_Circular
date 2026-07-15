#pragma once

#include <Arduino.h>

class AppLogger {
public:
  void begin();
  void info(const String &message);
  void warn(const String &message);
  void error(const String &message);
  String asText() const;

private:
  static const uint8_t MaxEntries = 40;
  String entries[MaxEntries];
  uint8_t nextIndex = 0;
  bool wrapped = false;

  void append(const String &level, const String &message);
};
