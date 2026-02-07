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
} chip8_t;

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

/**
 * Initialise the Chip-8 system.
 * Clears memory, registers, and loads the standard font set.
 *
 * @param c Pointer to the chip8_t instance to initialise.
 * @return true if initialisation was successful, false otherwise.
 */
bool chip8_init(chip8_t *c);

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

#endif // CHIP8_H
