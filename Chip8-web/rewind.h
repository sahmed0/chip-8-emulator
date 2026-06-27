#ifndef REWIND_H
#define REWIND_H

#include "chip8.h"
#include <stdbool.h>
#include <stddef.h>

// Ring buffer of full machine snapshots for the "rewind time" feature. Each slot
// is a raw chip8_t copy (the deterministic core makes the struct trivially
// copyable). Snapshots are pushed once per forward frame; rewinding pops them
// newest-first (LIFO). Capacity 600 = ~10s of history at 60 fps.
#define REWIND_CAPACITY 600

typedef struct {
  chip8_t frames[REWIND_CAPACITY]; // ~2.6 MB
  size_t head;                     // index of the next write
  size_t count;                    // number of valid snapshots (<= CAPACITY)
} rewind_buffer_t;

// Reset to empty. Call once before use and on emulator reset.
void rewind_init(rewind_buffer_t *rb);

// Copy state into the ring as the newest snapshot. When full, the oldest is
// overwritten. No-op if either pointer is NULL.
void rewind_push(rewind_buffer_t *rb, const chip8_t *state);

// Pop the newest snapshot into *out and remove it. Returns false (leaving *out
// untouched) when the ring is empty.
bool rewind_pop(rewind_buffer_t *rb, chip8_t *out);

// Number of stored snapshots.
size_t rewind_count(const rewind_buffer_t *rb);

#endif // REWIND_H
