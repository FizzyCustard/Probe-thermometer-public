#pragma once

#include <stdint.h>

// Fixed-size FIFO for work the async WebSocket callback hands to loop().
//
// Order matters for cook commands (start, then notes, then stop), so unlike
// the single-slot read queue in websocket_handler.h nothing is overwritten:
// push() fails when the ring is full and the caller decides what to do.
//
// No locking: on the ESP8266 the async callbacks and loop() never run at the
// same time, they take turns on one core.
template <typename T, uint8_t N>
struct DeferredQueue {
  T       items[N];
  uint8_t head  = 0;   // next item to pop
  uint8_t count = 0;

  bool push(const T& item) {
    if (count == N) return false;
    items[(head + count) % N] = item;
    count++;
    return true;
  }

  bool pop(T& out) {
    if (count == 0) return false;
    out  = items[head];
    head = (head + 1) % N;
    count--;
    return true;
  }
};
