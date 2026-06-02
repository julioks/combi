#pragma once

#include <Arduino.h>

struct EdgeEvent {
  uint32_t t_us;
  uint8_t level;
  uint8_t _pad0;
  uint16_t _pad1;
};
