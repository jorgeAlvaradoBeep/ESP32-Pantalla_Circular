#pragma once

#include <Arduino.h>

#include "AppConfig.h"
#include "AppLogger.h"
#include "Config.h"
#include "ConnectLifeClient.h"
#include "ControlTypes.h"
#include "Sensor.h"

// De dónde sale el objetivo que está persiguiendo el lazo ahora mismo.
enum class ControlSource : uint8_t {
  Disabled,   // el usuario apagó el control autónomo
  Idle,       // control activo pero ningún horario en curso
  Schedule,   // siguiendo un bloque horario
  Instant     // petición instantánea del usuario (pisa los horarios)
};

struct ControlStatus {
  bool autoEnabled = false;
  ControlSource source = ControlSource::Disabled;
  int8_t scheduleIndex = -1;
  float targetC = NAN;        // lo que pidió el usuario
  float filteredC = NAN;      // ambiente filtrado del sensor local
  float errorC = NAN;
  float biasC = 0.0f;         // corrección que el PI aplica sobre el objetivo
  int commandedSetpointC = 0; // setpoint virtual enviado al equipo
  bool hasCommanded = false;
  unsigned long lastCommandMs = 0;
  bool timeSynced = false;
  String note;
};

// Lazo externo de temperatura.
//
// El equipo solo acepta un setpoint entero y regula contra SU propio sensor,
// que está dentro de la unidad y no donde está el usuario. Por eso no basta con
// reenviarle el objetivo: este lazo mide con el sensor local, calcula el error
// contra lo que pidió el usuario y le manda al aire un setpoint "virtual"
// sesgado hasta que el cuarto realmente llegue a la temperatura pedida.
class TempControl {
public:
  TempControl(AppConfig &config,
              ConnectLifeClient &connectLife,
              Sensor &sensor,
              AppLogger &logger);

  void begin();
  void addSample(const SensorReading &reading);
  void loop(unsigned long nowMs);

  ControlStatus getStatus() const;
  ControlConfig getConfig() const;

  void setAutoEnabled(bool enabled);
  void setInstant(bool active, float targetC);
  void setMode(ControlMode mode);
  bool setSchedule(uint8_t index, const Schedule &schedule);

  // Llamado cuando el usuario toca el aire en crudo desde la página de
  // termostato. Apaga el control autónomo para no pelearse con él.
  void notifyManualOverride(const String &what);

  // Resumen corto para la pantalla circular.
  String displayLine() const;

  static float clampUserTarget(float target);

private:
  AppConfig &configStore;
  ConnectLifeClient &connectLife;
  Sensor &sensor;
  AppLogger &logger;

  ControlStatus status;

  float samples[CONTROL_FILTER_WINDOW];
  uint8_t sampleCount = 0;
  uint8_t sampleHead = 0;

  float integral = 0.0f;
  unsigned long lastTickMs = 0;
  unsigned long lastEvalMs = 0;
  bool engagedAc = false;       // ¿encendimos el aire nosotros?
  bool scheduleWasActive = false;
  float lastTargetC = NAN;

  void evaluate(unsigned long nowMs);
  bool filteredTemperature(float &out) const;
  bool resolveTarget(float &targetOut, ControlSource &sourceOut, int8_t &indexOut);
  bool scheduleActive(const Schedule &schedule, int minuteOfDay, int weekday) const;
  bool localNow(int &minuteOfDay, int &weekday) const;
  void engage(float targetC, bool forceSetup);
  void disengage();
  void resetLoopState();
  const char *modeName(ControlMode mode) const;
};
