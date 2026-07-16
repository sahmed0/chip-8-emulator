#ifndef CHIP8_H
#define CHIP8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Constants & Specifications
// -----------------------------------------------------------------------------

/**
 * Chip-8 System Specifications.
 * These constants define the hardware constraints of the original Chip-8
 * virtual machine.
 */
#define CHIP8_MEMORY_SIZE 4096            // Total RAM in bytes
#define CHIP8_PROGRAM_START_ADDRESS 0x200 // Entry point for ROMs (512)
#define CHIP8_FONT_START_ADDRESS 0x50     // Memory location for built-in font
#define CHIP8_FONT_SIZE 80                // Size of the font set in bytes
#define REGISTER_COUNT 16 // Number of 8-bit general purpose registers
#define STACK_SIZE 16     // Depth of the call stack
#define KEY_COUNT 16      // Number of keys in the hex keypad
#define DISPLAY_WIDTH 64  // Horizontal resolution in pixels
#define DISPLAY_HEIGHT 32 // Vertical resolution in pixels
#define DISPLAY_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT) // Total pixels

// Default PRNG seed substituted when a caller passes 0 (xorshift32 requires a
// nonzero state). Value is Marsaglia's canonical xorshift32 default.
#define CHIP8_DEFAULT_SEED 2463534242u

// Savestate blob format. The header lets chip8_load_state reject stale,
// truncated, or wrong-version blobs. Bump CHIP8_STATE_VERSION if chip8_t's
// layout ever changes. NOTE: the blob is host-endian and layout-specific - a
// same-machine save format, not a portable one.
// Version 2 added the quirks/vblank_wait fields to chip8_t, so v1 blobs (which
// lack them) are structurally incompatible and correctly rejected.
#define CHIP8_STATE_MAGIC   0x43385354u // 'C8ST'
#define CHIP8_STATE_VERSION 2u

// -----------------------------------------------------------------------------
// Dialect Quirks
// -----------------------------------------------------------------------------

/**
 * Dialect quirks. CHIP-8 has no single spec: the original COSMAC VIP
 * interpreter and the HP48 SCHIP interpreter disagree on six behaviors, and
 * ROMs written for one break on the other. Each flag selects one side of one
 * ambiguity; the presets below reproduce the two real machines.
 * Fields are uint8_t (not bool) so chip8_t stays a flat, padding-stable blob.
 */
typedef struct {
  uint8_t vf_reset;           // 8XY1/2/3 set VF=0 afterwards (VIP: 1, SCHIP: 0)
  uint8_t memory_increment_i; // FX55/FX65 leave I = I + X + 1 (VIP: 1, SCHIP: 0)
  uint8_t display_wait;       // DXYN halts execution until vblank (VIP: 1, SCHIP: 0)
  uint8_t clipping;           // Sprites clip at edges; start coords wrap (both: 1; 0 = full wrap)
  uint8_t shift_vy;           // 8XY6/8XYE shift Vy into Vx (VIP: 1) vs shift Vx in place (SCHIP: 0)
  uint8_t jump_vx;            // BXNN jumps to XNN + VX (SCHIP: 1) vs BNNN + V0 (VIP: 0)
} chip8_quirks_t;

// Profile ids for the FFI boundary (web UI dropdown indexes these).
typedef enum {
  CHIP8_PROFILE_VIP = 0,   // Original COSMAC VIP (default)
  CHIP8_PROFILE_SCHIP = 1, // HP48 Super-CHIP dialect (1.1, without hires)
} chip8_profile_t;

// -----------------------------------------------------------------------------
// Data Structures
// -----------------------------------------------------------------------------

/**
 * The core Chip-8 context.
 * Contains the entire state of the emulated machine, including memory,
 * registers, timers, and the display buffer.
 */
typedef struct {
  /**
   * System RAM.
   * 0x000-0x1FF: Reserved for interpreter (font set stored here).
   * 0x200-0xFFF: Program / ROM space.
   */
  uint8_t memory[CHIP8_MEMORY_SIZE];

  /**
   * General purpose 8-bit registers (V0 - VF).
   * VF is used as a flag register for carry, borrow, and collision detection.
   */
  uint8_t registers[REGISTER_COUNT];

  /**
   * Index Register (I).
   * Used to store memory addresses. Only the lowest 12 bits are usually used.
   */
  uint16_t index_register;

  /**
   * Program Counter (PC).
   * Stores the address of the currently executing instruction.
   */
  uint16_t program_counter;

  /**
   * Call Stack.
   * Stores return addresses when subroutines are called.
   */
  uint16_t stack[STACK_SIZE];

  /**
   * Stack Pointer (SP).
   * Points to the top level of the stack.
   */
  uint16_t stack_pointer;

  /**
   * Delay Timer.
   * Decrements at 60Hz. Used for game timing events.
   */
  uint8_t delay_timer;

  /**
   * Sound Timer.
   * Decrements at 60Hz. When non-zero, the system buzzes.
   */
  uint8_t sound_timer;

  /**
   * Keypad State.
   * Represents the 16-key hex keypad.
   * 1 = Pressed, 0 = Released.
   */
  uint8_t keypad[KEY_COUNT];

  /**
   * Graphics Buffer.
   * A flat array representing the 64x32 monochrome display.
   * 1 = Pixel On (White), 0 = Pixel Off (Black).
   */
  uint8_t display[DISPLAY_SIZE];

  /**
   * Draw Flag.
   * Set to true whenever a graphics opcode modifies the display buffer.
   * Effectively a "dirty" flag for the renderer to know when to redraw.
   */
  bool draw_flag;

  /**
   * PRNG state for the CXNN (RND) instruction - xorshift32 (Marsaglia 2003).
   * Lives inside chip8_t so the core is self-contained (no global libc rand)
   * and the whole struct is a flat POD blob: same seed + same input sequence
   * produce bit-identical execution. Seeded by chip8_init; always nonzero.
   */
  uint32_t rng_state;

  /** Active dialect quirks (see chip8_quirks_t). Set by chip8_init to the
   *  COSMAC VIP preset; hosts switch profiles via chip8_set_quirks. Lives in
   *  chip8_t so savestates and rewind snapshots carry the profile with them. */
  chip8_quirks_t quirks;

  /** Display-wait latch. When quirks.display_wait is set, DXYN draws and then
   *  sets this; the host stops executing cycles for the rest of the frame.
   *  chip8_update_timers (the 60 Hz "vblank") clears it. */
  uint8_t vblank_wait;
} chip8_t;

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

/**
 * Initialise the Chip-8 system.
 * Clears memory, registers, and loads the standard font set, then seeds the
 * deterministic PRNG. The core never touches global rand()/srand(): the same
 * seed + same input sequence always yields identical execution.
 *
 * @param c    Pointer to the chip8_t instance to initialise.
 * @param seed PRNG seed. Pass time(NULL) for a fresh run, or a constant for a
 *             reproducible one. A seed of 0 is replaced with CHIP8_DEFAULT_SEED.
 * @return true if initialisation was successful, false otherwise.
 */
bool chip8_init(chip8_t *c, uint32_t seed);

/**
 * Load a ROM into the system memory.
 *
 * @param c Pointer to the initialized chip8_t instance.
 * @param data Pointer to the raw ROM byte array.
 * @param size Size of the ROM data in bytes.
 */
void chip8_load_rom(chip8_t *c, const uint8_t *data, size_t size);

/**
 * Execute a single CPU cycle.
 * Performs the Fetch-Decode-Execute loop for one instruction.
 *
 * @param c Pointer to the chip8_t instance.
 */
void chip8_cycle(chip8_t *c);

/**
 * Update the system timers.
 * Should be called at a rate of 60Hz. Decrements delay and sound timers.
 *
 * @param c Pointer to the chip8_t instance.
 */
void chip8_update_timers(chip8_t *c);

/**
 * Update the state of a specific key.
 *
 * @param c Pointer to the chip8_t instance.
 * @param key The key index (0-15).
 * @param down true if pressed, false if released.
 */
void chip8_set_key(chip8_t *c, int key, bool down);

/**
 * Get a pointer to the display buffer.
 *
 * @param c Pointer to the chip8_t instance.
 * @return Pointer to the uint8_t display array.
 */
const uint8_t *chip8_get_display(chip8_t *c);

/**
 * Total size in bytes of a savestate blob (header + serialized state).
 * Callers allocate a buffer of at least this size before chip8_save_state.
 */
size_t chip8_state_size(void);

/**
 * Serialize the full machine state into buffer as a versioned blob.
 * @return true on success; false if buffer is NULL or smaller than
 *         chip8_state_size().
 */
bool chip8_save_state(const chip8_t *c, uint8_t *buffer, size_t buffer_size);

/**
 * Restore machine state from a blob produced by chip8_save_state. Validates the
 * magic, version, and payload size; on any mismatch, *c is left untouched.
 * @return true on success; false on NULL args, short buffer, or header mismatch.
 */
bool chip8_load_state(chip8_t *c, const uint8_t *buffer, size_t buffer_size);

/** Quirk preset reproducing the original COSMAC VIP interpreter. */
chip8_quirks_t chip8_quirks_vip(void);

/** Quirk preset reproducing the HP48 Super-CHIP dialect (no hires). */
chip8_quirks_t chip8_quirks_schip(void);

/** Replace the active quirk set. Takes effect from the next instruction. */
void chip8_set_quirks(chip8_t *c, chip8_quirks_t quirks);

#endif // CHIP8_H
