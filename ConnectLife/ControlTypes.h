#pragma once

#include <Arduino.h>

// Tipos compartidos entre AppConfig (persistencia) y TempControl (lazo de control).
// Viven en su propio header para evitar la dependencia circular
// AppConfig.h <-> TempControl.h.

static const uint8_t MAX_SCHEDULES = 3;

// Modo de operación que el lazo autónomo le pide al aire.
enum class ControlMode : uint8_t {
  Cool = 0,
  Heat = 1,
  Auto = 2
};

// Un bloque horario. Los minutos son locales, contados desde medianoche.
// Si endMinute <= startMinute el bloque cruza la medianoche.
struct Schedule {
  bool enabled = false;
  uint8_t daysMask = 0x7F;    // bit0 = domingo ... bit6 = sábado
  uint16_t startMinute = 0;
  uint16_t endMinute = 0;
  float targetC = 22.0f;
};

// Configuración persistida del control autónomo.
struct ControlConfig {
  bool autoEnabled = false;
  ControlMode mode = ControlMode::Cool;

  // Modula la velocidad del ventilador según lo que falte para el objetivo.
  bool fanControlEnabled = true;

  // Petición instantánea del usuario: pisa cualquier horario mientras esté activa.
  bool instantActive = false;
  float instantTargetC = 22.0f;

  Schedule schedules[MAX_SCHEDULES];
};
