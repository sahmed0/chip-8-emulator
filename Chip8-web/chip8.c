#include "chip8.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MEMORY_START_ADDRESS CHIP8_PROGRAM_START_ADDRESS
#define FONTSET_START_ADDRESS CHIP8_FONT_START_ADDRESS

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

/**
 * Standard Chip-8 Font Set.
 * Represents hexadecimal characters 0-F.
 * Each character is 5 bytes tall (8x5 pixels).
 */
static const uint8_t fontset[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

// -----------------------------------------------------------------------------
// Memory access helpers
// -----------------------------------------------------------------------------

// Chip-8 has a 12-bit address space (0x000-0xFFF). Masking every access with
// ADDRESS_MASK wraps any computed address back into the 4 KiB RAM, which both
// matches the original hardware's addressing and guarantees memory accesses stay
// in bounds. Routing all RAM reads/writes through these two accessors makes the
// bounds check structurally unavoidable: raw chip8->memory[...] indexing becomes
// the conspicuous exception rather than the easily-forgotten norm.
#define ADDRESS_MASK (CHIP8_MEMORY_SIZE - 1) // 0x0FFF for 4096-byte RAM

static inline uint8_t mem_read(const chip8_t *chip8, uint16_t addr) {
  return chip8->memory[addr & ADDRESS_MASK];
}

static inline void mem_write(chip8_t *chip8, uint16_t addr, uint8_t value) {
  chip8->memory[addr & ADDRESS_MASK] = value;
}

// -----------------------------------------------------------------------------
// Core Implementation
// -----------------------------------------------------------------------------

bool chip8_init(chip8_t *chip8) {
  if (!chip8)
    return false;

  // Clear the entire context (RAM, registers, display, etc.) to zero.
  memset(chip8, 0, sizeof(chip8_t));

  // Set the Program Counter to the start address of the ROM (0x200).
  chip8->program_counter = MEMORY_START_ADDRESS;

  // Load the built-in font set into memory (0x050 - 0x0A0).
  for (int i = 0; i < CHIP8_FONT_SIZE; ++i) {
    mem_write(chip8, FONTSET_START_ADDRESS + i, fontset[i]);
  }

  // Seed the random number generator for the RND (0xCXXX) instruction.
  srand((unsigned int)time(NULL));

  return true;
}

void chip8_load_rom(chip8_t *chip8, const uint8_t *data, size_t size) {
  if (!chip8 || !data)
    return;

  // Safe Guard: Ensure the ROM fits within the available memory space.
  // Available: 4096 (Total) - 512 (Reserved) = 3584 bytes.
  if (size > (CHIP8_MEMORY_SIZE - MEMORY_START_ADDRESS)) {
    // ROM is too large; in a real scenario, we might want to log an error here.
    return;
  }

  // Copy the ROM data directly into the program memory space.
  memcpy(&chip8->memory[MEMORY_START_ADDRESS], data, size);
}

/**
 * Execute one CPU cycle: Fetch, Decode, Execute.
 */
void chip8_cycle(chip8_t *chip8) {
  if (!chip8)
    return;

  // ---------------------------------------------------------------------------
  // 1. Fetch
  // ---------------------------------------------------------------------------
  // Opcodes are 2 bytes (16-bit). Memory is 1 byte (8-bit).
  // We merge two bytes to form the opcode: (MSB << 8) | LSB.
  // Chip-8 has a 12-bit address space; mem_read wraps each byte into range.
  uint16_t opcode = (mem_read(chip8, chip8->program_counter) << 8) |
                    mem_read(chip8, chip8->program_counter + 1);

  // Advance Program Counter to the next instruction.
  chip8->program_counter += 2;

  // ---------------------------------------------------------------------------
  // 2. Decode variables
  // ---------------------------------------------------------------------------
  // Extract common operands from the opcode:
  // X: The second nibble. Used as a register index (V[x]).
  uint8_t reg_x = (opcode & 0x0F00) >> 8;
  // Y: The third nibble. Used as a register index (V[y]).
  uint8_t reg_y = (opcode & 0x00F0) >> 4;
  // N: The fourth nibble. A 4-bit constant.
  // uint8_t n = opcode & 0x000F; // Unused variable commented out

  // NN: The last byte. An 8-bit constant.
  uint8_t byte_val = opcode & 0x00FF;
  // NNN: The last 12 bits. A 12-bit memory address.
  uint16_t addr_val = opcode & 0x0FFF;

  // ---------------------------------------------------------------------------
  // 3. Execute
  // ---------------------------------------------------------------------------
  // Dispatch based on the first nibble (the major opcode category).
  switch (opcode & 0xF000) {
  case 0x0000:
    switch (opcode & 0x00FF) {
    case 0xE0: // 00E0: CLS (Clear Display)
      memset(chip8->display, 0, sizeof(chip8->display));
      chip8->draw_flag = true;
      break;
    case 0xEE: // 00EE: RET (Return from subroutine)
      if (chip8->stack_pointer > 0) {
        chip8->stack_pointer--;
        chip8->program_counter = chip8->stack[chip8->stack_pointer];
      }
      break;
    default:
      // 0NNN is ignored (Sys call calls RCA 1802 program at NNN)
      break;
    }
    break;

  case 0x1000: // 1NNN: JP addr (Jump to address NNN)
    chip8->program_counter = addr_val;
    break;

  case 0x2000: // 2NNN: CALL addr (Call subroutine at NNN)
    if (chip8->stack_pointer < STACK_SIZE) {
      chip8->stack[chip8->stack_pointer] = chip8->program_counter;
      chip8->stack_pointer++;
      chip8->program_counter = addr_val;
    }
    break;

  case 0x3000: // 3XNN: SE Vx, byte (Skip Next Instruction If Vx == NN)
    if (chip8->registers[reg_x] == byte_val)
      chip8->program_counter += 2;
    break;

  case 0x4000: // 4XNN: SNE Vx, byte (Skip Next Instruction If Vx != NN)
    if (chip8->registers[reg_x] != byte_val)
      chip8->program_counter += 2;
    break;

  case 0x5000: // 5XY0: SE Vx, Vy (Skip Next Instruction If Vx == Vy)
    if (chip8->registers[reg_x] == chip8->registers[reg_y])
      chip8->program_counter += 2;
    break;

  case 0x6000: // 6XNN: LD Vx, byte (Set Vx = NN)
    chip8->registers[reg_x] = byte_val;
    break;

  case 0x7000: // 7XNN: ADD Vx, byte (Set Vx = Vx + NN)
    // Note: This adds the value to Vx. Does NOT affect the carry flag.
    chip8->registers[reg_x] += byte_val;
    break;

  case 0x8000: // 8XYN: Arithmetic / Logic Operations
  {
    switch (opcode & 0x000F) {
    case 0x0: // LD Vx, Vy
      chip8->registers[reg_x] = chip8->registers[reg_y];
      break;
    case 0x1: // OR Vx, Vy
      chip8->registers[reg_x] |= chip8->registers[reg_y];
      break;
    case 0x2: // AND Vx, Vy
      chip8->registers[reg_x] &= chip8->registers[reg_y];
      break;
    case 0x3: // XOR Vx, Vy
      chip8->registers[reg_x] ^= chip8->registers[reg_y];
      break;
    case 0x4: // ADD Vx, Vy (Set VF = carry)
    {
      uint16_t sum = chip8->registers[reg_x] + chip8->registers[reg_y];
      // IF sum > 255, we have an overflow (Carry). VF = 1.
      uint8_t flag = (sum > 255) ? 1 : 0;
      chip8->registers[reg_x] = sum & 0xFF; // Store only the lowest 8 bits
      // Write VF last so the flag survives even when Vx == VF.
      chip8->registers[0xF] = flag;
    } break;
    case 0x5: // SUB Vx, Vy (Set VF = NOT borrow)
    {
      // If Vx >= Vy, there is no borrow. VF = 1.
      uint8_t flag =
          (chip8->registers[reg_x] >= chip8->registers[reg_y]) ? 1 : 0;
      chip8->registers[reg_x] =
          chip8->registers[reg_x] - chip8->registers[reg_y];
      // Write VF last so the flag survives even when Vx == VF.
      chip8->registers[0xF] = flag;
    } break;
    case 0x6: // SHR Vx (Shift Right)
    {
      // Ambiguity: Modern Chip-8 implementations (Schip) shift Vx.
      // Original COSMAC VIP shifted Vy into Vx. We use modern behavior (Vx).
      // Save LSB for VF.
      uint8_t flag = chip8->registers[reg_x] & 0x1;
      chip8->registers[reg_x] >>= 1;
      // Write VF last so the flag survives even when Vx == VF.
      chip8->registers[0xF] = flag;
    } break;
    case 0x7: // SUBN Vx, Vy (Set VF = NOT borrow)
    {
      // If Vy >= Vx, there is no borrow. VF = 1.
      uint8_t flag =
          (chip8->registers[reg_y] >= chip8->registers[reg_x]) ? 1 : 0;
      chip8->registers[reg_x] =
          chip8->registers[reg_y] - chip8->registers[reg_x];
      // Write VF last so the flag survives even when Vx == VF.
      chip8->registers[0xF] = flag;
    } break;
    case 0xE: // SHL Vx (Shift Left)
    {
      // Save MSB for VF.
      uint8_t flag = (chip8->registers[reg_x] >> 7) & 0x1;
      chip8->registers[reg_x] <<= 1;
      // Write VF last so the flag survives even when Vx == VF.
      chip8->registers[0xF] = flag;
    } break;
    }
  } break;

  case 0x9000: // 9XY0: SNE Vx, Vy (Skip Next If Vx != Vy)
    if (chip8->registers[reg_x] != chip8->registers[reg_y])
      chip8->program_counter += 2;
    break;

  case 0xA000: // ANNN: LD I, addr (Set I = NNN)
    chip8->index_register = addr_val;
    break;

  case 0xB000: // BNNN: JP V0, addr (Jump to V0 + NNN)
    chip8->program_counter = addr_val + chip8->registers[0];
    break;

  case 0xC000: // CXNN: RND Vx, byte (Set Vx = random byte & NN)
    chip8->registers[reg_x] = (rand() % 256) & byte_val;
    break;

  case 0xD000: // DXYN: DRW Vx, Vy, nibble (Draw Sprite)
  {
    uint8_t height = opcode & 0x000F;
    uint8_t pixel;

    // Default VF to 0 (no collision).
    chip8->registers[0xF] = 0;

    for (int yline = 0; yline < height; yline++) {
      // Fetch the pixel byte from memory at I + row index
      pixel = mem_read(chip8, chip8->index_register + yline);

      for (int xline = 0; xline < 8; xline++) {
        // Check each bit (pixel) in the byte (MSB to LSB calls 0x80 >> x)
        if ((pixel & (0x80 >> xline)) != 0) {
          int target_x = (chip8->registers[reg_x] + xline) % DISPLAY_WIDTH;
          int target_y = (chip8->registers[reg_y] + yline) % DISPLAY_HEIGHT;
          int index = target_y * DISPLAY_WIDTH + target_x;

          // XOR Drawing Logic:
          // If the pixel on screen is 1 and we draw a 1, it turns off (0).
          // This collision sets VF to 1.
          if (chip8->display[index] == 1) {
            chip8->registers[0xF] = 1;
            chip8->display[index] = 0;
          } else {
            chip8->display[index] = 1;
          }
        }
      }
    }
    // Flag the system that a redraw is required.
    chip8->draw_flag = true;
  } break;

  case 0xE000: // Key OpCodes
  {
    switch (opcode & 0x00FF) {
    case 0x9E: // EX9E: SKP Vx (Skip next if key stored in Vx is pressed)
      // Keys are a single hex nibble; mask to keep the keypad index in range.
      if (chip8->keypad[chip8->registers[reg_x] & 0x0F] != 0)
        chip8->program_counter += 2;
      break;
    case 0xA1: // EXA1: SKNP Vx (Skip next if key stored in Vx is NOT pressed)
      if (chip8->keypad[chip8->registers[reg_x] & 0x0F] == 0)
        chip8->program_counter += 2;
      break;
    }
  } break;

  case 0xF000: // Misc
  {
    switch (opcode & 0x00FF) {
    case 0x07: // LD Vx, DT (Set Vx = delay timer)
      chip8->registers[reg_x] = chip8->delay_timer;
      break;
    case 0x0A: // FX0A: Wait for key press
    {
      bool key_pressed = false;
      for (int i = 0; i < KEY_COUNT; i++) {
        if (chip8->keypad[i] != 0) {
          chip8->registers[reg_x] = i;
          key_pressed = true;
          break;
        }
      }
      // If no key is pressed, we decrement PC to "re-execute" this instruction
      // on the next cycle, effectively pausing execution until a key is down.
      if (!key_pressed) {
        chip8->program_counter -= 2;
      }
    } break;

    case 0x15: // LD DT, Vx (Set delay timer = Vx)
      chip8->delay_timer = chip8->registers[reg_x];
      break;
    case 0x18: // LD ST, Vx (Set sound timer = Vx)
      chip8->sound_timer = chip8->registers[reg_x];
      break;
    case 0x1E: // ADD I, Vx (Set I = I + Vx)
      chip8->index_register += chip8->registers[reg_x];
      break;
    case 0x29: // LD F, Vx (Set I = location of sprite for digit Vx)
      // Glyphs are 5 bytes long. Offset = Digit * 5.
      chip8->index_register =
          FONTSET_START_ADDRESS + (chip8->registers[reg_x] * 5);
      break;
    case 0x33: // LD B, Vx (Store BCD representation of Vx at I, I+1, I+2)
      mem_write(chip8, chip8->index_register, chip8->registers[reg_x] / 100);
      mem_write(chip8, chip8->index_register + 1,
                (chip8->registers[reg_x] / 10) % 10);
      mem_write(chip8, chip8->index_register + 2, chip8->registers[reg_x] % 10);
      break;
    case 0x55: // LD [I], Vx (Dump registers V0-Vx into memory at I)
      for (int i = 0; i <= reg_x; i++)
        mem_write(chip8, chip8->index_register + i, chip8->registers[i]);
      break;
    case 0x65: // LD Vx, [I] (Load memory at I into registers V0-Vx)
      for (int i = 0; i <= reg_x; i++)
        chip8->registers[i] = mem_read(chip8, chip8->index_register + i);
      break;
    }
  } break;

  default:
    // Log unknown opcode if necessary, or just ignore.
    break;
  }
}

void chip8_update_timers(chip8_t *chip8) {
  if (!chip8)
    return;

  if (chip8->delay_timer > 0)
    chip8->delay_timer--;
  if (chip8->sound_timer > 0)
    chip8->sound_timer--;
}

void chip8_set_key(chip8_t *chip8, int key, bool down) {
  if (!chip8 || key < 0 || key >= KEY_COUNT)
    return;
  chip8->keypad[key] = down ? 1 : 0;
}

const uint8_t *chip8_get_display(chip8_t *chip8) {
  if (!chip8)
    return NULL;
  return chip8->display;
}
