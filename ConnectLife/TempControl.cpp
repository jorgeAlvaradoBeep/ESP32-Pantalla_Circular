#include "TempControl.h"

#include <math.h>
#include <time.h>

#include "Config.h"

TempControl::TempControl(AppConfig &configRef,
                         ConnectLifeClient &connectLifeRef,
                         Sensor &sensorRef,
                         AppLogger &loggerRef)
    : configStore(configRef),
      connectLife(connectLifeRef),
      sensor(sensorRef),
      logger(loggerRef)
{
  for (uint8_t i = 0; i < CONTROL_FILTER_WINDOW; i++) {
    samples[i] = NAN;
  }
}

void TempControl::begin()
{
  const ControlConfig &cfg = configStore.getControl();
  status.autoEnabled = cfg.autoEnabled;
  status.source = cfg.autoEnabled ? ControlSource::Idle : ControlSource::Disabled;
  status.note = cfg.autoEnabled ? "Control autónomo activo" : "Control autónomo apagado";
  logger.info(String("Temp control ") + (cfg.autoEnabled ? "enabled" : "disabled") +
              ", mode " + modeName(cfg.mode));
}

float TempControl::clampUserTarget(float target)
{
  if (isnan(target)) {
    return CONTROL_USER_MIN_C;
  }
  if (target < CONTROL_USER_MIN_C) return CONTROL_USER_MIN_C;
  if (target > CONTROL_USER_MAX_C) return CONTROL_USER_MAX_C;
  return target;
}

const char *TempControl::modeName(ControlMode mode) const
{
  switch (mode) {
    case ControlMode::Heat: return "heat";
    case ControlMode::Auto: return "auto";
    case ControlMode::Cool:
    default: return "cool";
  }
}

void TempControl::addSample(const SensorReading &reading)
{
  if (!reading.valid || isnan(reading.temperatureC)) {
    return;
  }
  samples[sampleHead] = reading.temperatureC;
  sampleHead = (sampleHead + 1) % CONTROL_FILTER_WINDOW;
  if (sampleCount < CONTROL_FILTER_WINDOW) {
    sampleCount++;
  }
}

bool TempControl::filteredTemperature(float &out) const
{
  if (sampleCount == 0) {
    return false;
  }
  float sum = 0.0f;
  uint8_t used = 0;
  for (uint8_t i = 0; i < CONTROL_FILTER_WINDOW; i++) {
    if (!isnan(samples[i])) {
      sum += samples[i];
      used++;
    }
  }
  if (used == 0) {
    return false;
  }
  out = sum / used;
  return true;
}

bool TempControl::localNow(int &minuteOfDay, int &weekday) const
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) {
    return false;
  }
  minuteOfDay = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  weekday = timeinfo.tm_wday;  // 0 = domingo
  return true;
}

bool TempControl::scheduleActive(const Schedule &schedule, int minuteOfDay, int weekday) const
{
  if (!schedule.enabled || schedule.daysMask == 0) {
    return false;
  }
  const bool today = (schedule.daysMask & (1 << weekday)) != 0;

  if (schedule.startMinute < schedule.endMinute) {
    return today && minuteOfDay >= schedule.startMinute && minuteOfDay < schedule.endMinute;
  }

  // Bloque que cruza medianoche: el día marcado es el del arranque, así que la
  // cola de madrugada pertenece al día anterior.
  const int yesterday = (weekday + 6) % 7;
  const bool startedYesterday = (schedule.daysMask & (1 << yesterday)) != 0;
  if (today && minuteOfDay >= schedule.startMinute) {
    return true;
  }
  return startedYesterday && minuteOfDay < schedule.endMinute;
}

bool TempControl::resolveTarget(float &targetOut, ControlSource &sourceOut, int8_t &indexOut)
{
  ControlConfig cfg = configStore.getControl();

  int minuteOfDay = 0;
  int weekday = 0;
  const bool haveTime = localNow(minuteOfDay, weekday);
  status.timeSynced = haveTime;

  int8_t activeIndex = -1;
  if (haveTime) {
    for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
      if (scheduleActive(cfg.schedules[i], minuteOfDay, weekday)) {
        activeIndex = static_cast<int8_t>(i);
        break;
      }
    }
  }

  // Al arrancar un bloque horario nuevo, la petición instantánea caduca: el
  // usuario pidió "ahora", no "para siempre".
  const bool scheduleIsActive = activeIndex >= 0;
  if (scheduleIsActive && !scheduleWasActive && cfg.instantActive) {
    cfg.instantActive = false;
    configStore.saveControl(cfg);
    logger.info("Instant request cleared by start of a scheduled block");
  }
  scheduleWasActive = scheduleIsActive;

  if (cfg.instantActive) {
    targetOut = clampUserTarget(cfg.instantTargetC);
    sourceOut = ControlSource::Instant;
    indexOut = -1;
    return true;
  }

  if (scheduleIsActive) {
    targetOut = clampUserTarget(cfg.schedules[activeIndex].targetC);
    sourceOut = ControlSource::Schedule;
    indexOut = activeIndex;
    return true;
  }

  sourceOut = ControlSource::Idle;
  indexOut = -1;
  return false;
}

void TempControl::resetLoopState()
{
  integral = 0.0f;
  status.biasC = 0.0f;
  status.hasCommanded = false;
  status.commandedSetpointC = 0;
}

void TempControl::engage(float targetC, bool forceSetup)
{
  if (!forceSetup) {
    return;
  }
  const ControlConfig &cfg = configStore.getControl();
  logger.info("Control engaging: target " + String(targetC, 1) + " C, mode " + modeName(cfg.mode));
  // setMode() ya enciende el equipo (t_power = 1) además de fijar el modo.
  if (!connectLife.setMode(String(modeName(cfg.mode)))) {
    logger.warn("Could not set AC mode when engaging control");
  }
  engagedAc = true;
}

void TempControl::disengage()
{
  if (!engagedAc) {
    return;
  }
  logger.info("Control disengaging: turning AC off");
  if (!connectLife.setPower(false)) {
    logger.warn("Could not turn AC off when disengaging control");
  }
  engagedAc = false;
  resetLoopState();
  lastTargetC = NAN;
}

void TempControl::loop(unsigned long nowMs)
{
  if (!configStore.getControl().autoEnabled) {
    if (status.source != ControlSource::Disabled) {
      status.source = ControlSource::Disabled;
      status.autoEnabled = false;
      status.note = "Control autónomo apagado";
    }
    return;
  }

  if (lastTickMs != 0 && nowMs - lastTickMs < CONTROL_TICK_INTERVAL_MS) {
    return;
  }
  lastTickMs = nowMs;
  evaluate(nowMs);
}

void TempControl::evaluate(unsigned long nowMs)
{
  status.autoEnabled = true;

  float target = NAN;
  ControlSource source = ControlSource::Idle;
  int8_t index = -1;

  if (!resolveTarget(target, source, index)) {
    status.source = ControlSource::Idle;
    status.scheduleIndex = -1;
    status.targetC = NAN;
    status.errorC = NAN;
    status.note = status.timeSynced ? "Sin horario activo" : "Esperando hora (NTP)";
    disengage();
    return;
  }

  status.source = source;
  status.scheduleIndex = index;
  status.targetC = target;

  float filtered = NAN;
  if (!filteredTemperature(filtered)) {
    status.note = "Sin lectura del sensor";
    return;
  }
  status.filteredC = filtered;

  // Cambio de objetivo o arranque: el integrador acumulado ya no aplica.
  const bool targetChanged = isnan(lastTargetC) || fabsf(lastTargetC - target) > 0.05f;
  const bool starting = !engagedAc || targetChanged;
  if (starting) {
    resetLoopState();
    lastEvalMs = nowMs;
    lastTargetC = target;
    engage(target, true);
  }

  const float dtMinutes = lastEvalMs == 0 ? 1.0f : (nowMs - lastEvalMs) / 60000.0f;
  lastEvalMs = nowMs;

  const float error = target - filtered;
  status.errorC = error;

  // Fuera de la banda muerta integramos; dentro congelamos el integrador para
  // no acumular ruido del sensor cuando ya estamos en objetivo.
  if (fabsf(error) > CONTROL_DEADBAND_C) {
    integral += error * dtMinutes;
    const float integralLimit = CONTROL_MAX_BIAS_C / CONTROL_KI;
    if (integral > integralLimit) integral = integralLimit;
    if (integral < -integralLimit) integral = -integralLimit;
  }

  float bias = CONTROL_KP * error + CONTROL_KI * integral;
  if (bias > CONTROL_MAX_BIAS_C) bias = CONTROL_MAX_BIAS_C;
  if (bias < -CONTROL_MAX_BIAS_C) bias = -CONTROL_MAX_BIAS_C;
  status.biasC = bias;

  int desired = static_cast<int>(lroundf(target + bias));
  if (desired < CONTROL_AC_MIN_SETPOINT_C) desired = CONTROL_AC_MIN_SETPOINT_C;
  if (desired > CONTROL_AC_MAX_SETPOINT_C) desired = CONTROL_AC_MAX_SETPOINT_C;

  const bool sameAsCommanded = status.hasCommanded && desired == status.commandedSetpointC;
  const bool rateLimited = status.lastCommandMs != 0 &&
                           nowMs - status.lastCommandMs < CONTROL_MIN_COMMAND_INTERVAL_MS;

  if (sameAsCommanded) {
    status.note = "En régimen, setpoint " + String(desired) + " C";
    return;
  }
  // El límite de ritmo protege al compresor de ciclos cortos y evita saturar la
  // nube; no aplica al primer comando tras arrancar un bloque.
  if (rateLimited && !starting) {
    status.note = "Esperando para ajustar a " + String(desired) + " C";
    return;
  }

  if (connectLife.setTargetTemperature(desired)) {
    status.commandedSetpointC = desired;
    status.hasCommanded = true;
    status.lastCommandMs = nowMs;
    status.note = "Setpoint " + String(desired) + " C (objetivo " + String(target, 1) + " C)";
    logger.info("Control -> AC setpoint " + String(desired) + " C | ambiente " +
                String(filtered, 1) + " C | objetivo " + String(target, 1) +
                " C | sesgo " + String(bias, 2));
  } else {
    status.note = "Fallo al enviar setpoint";
    logger.warn("Control could not send setpoint " + String(desired));
  }
}

void TempControl::setAutoEnabled(bool enabled)
{
  ControlConfig cfg = configStore.getControl();
  if (cfg.autoEnabled == enabled) {
    return;
  }
  cfg.autoEnabled = enabled;
  configStore.saveControl(cfg);
  status.autoEnabled = enabled;

  if (!enabled) {
    // Apagar el control no apaga el aire: lo deja como esté para que el usuario
    // tome el mando.
    engagedAc = false;
    resetLoopState();
    lastTargetC = NAN;
    status.source = ControlSource::Disabled;
    status.note = "Control autónomo apagado";
  } else {
    resetLoopState();
    lastTargetC = NAN;
    lastTickMs = 0;   // evalúa en el próximo loop, sin esperar el minuto
    lastEvalMs = 0;
    status.source = ControlSource::Idle;
    status.note = "Control autónomo activo";
  }
  logger.info(String("Auto control ") + (enabled ? "enabled" : "disabled"));
}

void TempControl::setInstant(bool active, float targetC)
{
  ControlConfig cfg = configStore.getControl();
  cfg.instantActive = active;
  if (active) {
    cfg.instantTargetC = clampUserTarget(targetC);
    cfg.autoEnabled = true;   // pedir "ahora" implica querer el control encendido
  }
  configStore.saveControl(cfg);

  resetLoopState();
  lastTargetC = NAN;
  lastTickMs = 0;
  lastEvalMs = 0;
  status.autoEnabled = cfg.autoEnabled;
  logger.info(active ? "Instant target set to " + String(cfg.instantTargetC, 1) + " C"
                     : String("Instant target cleared"));
}

void TempControl::setMode(ControlMode mode)
{
  ControlConfig cfg = configStore.getControl();
  if (cfg.mode == mode) {
    return;
  }
  cfg.mode = mode;
  configStore.saveControl(cfg);
  lastTargetC = NAN;   // fuerza re-configurar el equipo en la próxima evaluación
  logger.info(String("Control mode set to ") + modeName(mode));
}

bool TempControl::setSchedule(uint8_t index, const Schedule &schedule)
{
  if (index >= MAX_SCHEDULES) {
    return false;
  }
  if (schedule.startMinute > 1439 || schedule.endMinute > 1439) {
    return false;
  }
  ControlConfig cfg = configStore.getControl();
  cfg.schedules[index] = schedule;
  cfg.schedules[index].targetC = clampUserTarget(schedule.targetC);
  configStore.saveControl(cfg);
  lastTargetC = NAN;
  logger.info("Schedule " + String(index) + " saved");
  return true;
}

void TempControl::notifyManualOverride(const String &what)
{
  if (!configStore.getControl().autoEnabled) {
    return;
  }
  logger.warn("Manual AC command (" + what + ") disabled autonomous control");
  setAutoEnabled(false);
  status.note = "Apagado por mando manual";
}

ControlStatus TempControl::getStatus() const
{
  return status;
}

ControlConfig TempControl::getConfig() const
{
  return configStore.getControl();
}

String TempControl::displayLine() const
{
  if (!status.autoEnabled) {
    return "Auto apagado";
  }
  switch (status.source) {
    case ControlSource::Instant:
      return "Auto " + String(status.targetC, 1) + "C ahora";
    case ControlSource::Schedule:
      return "Auto " + String(status.targetC, 1) + "C prog " + String(status.scheduleIndex + 1);
    case ControlSource::Idle:
      return status.timeSynced ? "Auto en espera" : "Auto sin hora";
    default:
      return "Auto apagado";
  }
}
