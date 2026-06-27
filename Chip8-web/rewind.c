#include "rewind.h"

void rewind_init(rewind_buffer_t *rb) {
  if (!rb)
    return;
  rb->head = 0;
  rb->count = 0;
}

void rewind_push(rewind_buffer_t *rb, const chip8_t *state) {
  if (!rb || !state)
    return;
  rb->frames[rb->head] = *state; // raw struct copy
  rb->head = (rb->head + 1) % REWIND_CAPACITY;
  if (rb->count < REWIND_CAPACITY)
    rb->count++;
  // When full, head has wrapped onto the oldest slot, which the line above just
  // overwrote - count stays pinned at REWIND_CAPACITY.
}

bool rewind_pop(rewind_buffer_t *rb, chip8_t *out) {
  if (!rb || !out || rb->count == 0)
    return false;
  rb->head = (rb->head + REWIND_CAPACITY - 1) % REWIND_CAPACITY;
  *out = rb->frames[rb->head];
  rb->count--;
  return true;
}

size_t rewind_count(const rewind_buffer_t *rb) {
  return rb ? rb->count : 0;
}
