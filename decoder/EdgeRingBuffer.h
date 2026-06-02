#pragma once

#include <Arduino.h>
#include "EdgeEvent.h"

bool popEdge(EdgeEvent &out);
void IRAM_ATTR pushEdgeFromIsr(uint32_t t_us, uint8_t level);
uint32_t getIsrDropCount();
