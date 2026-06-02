#include "EdgeRingBuffer.h"

#include "Config.h"

static EdgeEvent edgeRing[RING_SIZE];

// Single producer: ISR.
// Single consumer: decoder task.
static volatile uint32_t ringHead = 0;
static volatile uint32_t ringTail = 0;
static volatile uint32_t isrDropCount = 0;

bool popEdge(EdgeEvent &out) {
  const uint32_t tail = ringTail;

  if (tail == ringHead) {
    return false;
  }

  out = edgeRing[tail];

  // This is where the consumed ring slot becomes available again.
  ringTail = (tail + 1) & RING_MASK;

  return true;
}

void IRAM_ATTR pushEdgeFromIsr(uint32_t t_us, uint8_t level) {
  const uint32_t head = ringHead;
  const uint32_t nextHead = (head + 1) & RING_MASK;

  if (nextHead == ringTail) {
    isrDropCount++;
    return;
  }

  edgeRing[head].t_us = t_us;
  edgeRing[head].level = level;

  // Publish only after the event fields are written.
  __asm__ __volatile__("" ::: "memory");
  ringHead = nextHead;
}

uint32_t getIsrDropCount() {
  return isrDropCount;
}
