// Unit tests for the Chip-8 core (chip8.c).
//
// The core is SDL-free, so these tests link directly against chip8.c with plain
// gcc -- no SDL, no window, no Emscripten. Build & run via `make test` from the
// Chip8-web directory (see the Makefile `test` target).
//
// Framework: greatest (single-header, vendored as tests/greatest.h).

#include "greatest.h"

#include "../chip8.h"

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Place a single opcode at the program start (0x200) so that exactly one
// chip8_cycle() executes it. Opcodes are big-endian: high byte first.
static void load_opcode(chip8_t *c, uint16_t opcode) {
  c->memory[CHIP8_PROGRAM_START_ADDRESS] = (uint8_t)(opcode >> 8);
  c->memory[CHIP8_PROGRAM_START_ADDRESS + 1] = (uint8_t)(opcode & 0xFF);
}

// Fresh, initialised machine with one opcode loaded and ready to step.
static chip8_t make_machine(uint16_t opcode) {
  chip8_t c;
  chip8_init(&c);
  load_opcode(&c, opcode);
  return c;
}

// -----------------------------------------------------------------------------
// 8XY5: SUB Vx, Vy  (Set VF = NOT borrow)  -- the `>=` fix
// -----------------------------------------------------------------------------

// The core of the fix: Vx == Vy must be treated as "no borrow" (VF = 1).
// With the old `>` comparison this set VF = 0 incorrectly.
TEST sub_equal_operands_sets_no_borrow(void) {
  chip8_t c = make_machine(0x8015); // SUB V0, V1
  c.registers[0] = 5;
  c.registers[1] = 5;
  chip8_cycle(&c);
  ASSERT_EQ_FMT(0, c.registers[0], "%d");   // 5 - 5
  ASSERT_EQ_FMT(1, c.registers[0xF], "%d"); // equal -> no borrow
  PASS();
}

TEST sub_larger_minus_smaller_sets_no_borrow(void) {
  chip8_t c = make_machine(0x8015); // SUB V0, V1
  c.registers[0] = 10;
  c.registers[1] = 3;
  chip8_cycle(&c);
  ASSERT_EQ_FMT(7, c.registers[0], "%d");
  ASSERT_EQ_FMT(1, c.registers[0xF], "%d");
  PASS();
}

TEST sub_smaller_minus_larger_sets_borrow_and_wraps(void) {
  chip8_t c = make_machine(0x8015); // SUB V0, V1
  c.registers[0] = 3;
  c.registers[1] = 10;
  chip8_cycle(&c);
  ASSERT_EQ_FMT(0xF9, c.registers[0], "0x%02X"); // 3 - 10 wraps to 249
  ASSERT_EQ_FMT(0, c.registers[0xF], "%d");      // borrow occurred
  PASS();
}

// -----------------------------------------------------------------------------
// 8XY7: SUBN Vx, Vy  (Set VF = NOT borrow; Vx = Vy - Vx) -- same `>=` fix
// -----------------------------------------------------------------------------

TEST subn_equal_operands_sets_no_borrow(void) {
  chip8_t c = make_machine(0x8017); // SUBN V0, V1
  c.registers[0] = 8;
  c.registers[1] = 8;
  chip8_cycle(&c);
  ASSERT_EQ_FMT(0, c.registers[0], "%d");
  ASSERT_EQ_FMT(1, c.registers[0xF], "%d");
  PASS();
}

TEST subn_borrow_wraps(void) {
  chip8_t c = make_machine(0x8017); // SUBN V0, V1  => V0 = V1 - V0
  c.registers[0] = 10;
  c.registers[1] = 3;
  chip8_cycle(&c);
  ASSERT_EQ_FMT(0xF9, c.registers[0], "0x%02X"); // 3 - 10 wraps to 249
  ASSERT_EQ_FMT(0, c.registers[0xF], "%d");
  PASS();
}

// -----------------------------------------------------------------------------
// "Write VF last" fix: the flag must survive even when Vx IS VF.
// If VF were written before the result, the arithmetic would clobber the flag.
// -----------------------------------------------------------------------------

// 8XY4 ADD with carry, writing into VF: result is discarded, flag stays.
TEST add_carry_into_vf_keeps_flag(void) {
  chip8_t c = make_machine(0x8F14); // ADD VF, V1
  c.registers[0xF] = 200;
  c.registers[1] = 100; // 200 + 100 = 300 -> overflow
  chip8_cycle(&c);
  ASSERT_EQ_FMT(1, c.registers[0xF], "%d"); // carry flag, not 44
  PASS();
}

TEST add_no_carry_into_vf_keeps_flag(void) {
  chip8_t c = make_machine(0x8F14); // ADD VF, V1
  c.registers[0xF] = 10;
  c.registers[1] = 20; // 30, no overflow
  chip8_cycle(&c);
  ASSERT_EQ_FMT(0, c.registers[0xF], "%d");
  PASS();
}

// 8XY5 SUB into VF.
TEST sub_into_vf_keeps_flag(void) {
  chip8_t c = make_machine(0x8F15); // SUB VF, V1
  c.registers[0xF] = 50;
  c.registers[1] = 20; // 50 >= 20 -> no borrow
  chip8_cycle(&c);
  ASSERT_EQ_FMT(1, c.registers[0xF], "%d"); // flag wins over result (30)
  PASS();
}

// 8XY6 SHR into VF.
TEST shr_into_vf_keeps_flag(void) {
  chip8_t c = make_machine(0x8F06); // SHR VF
  c.registers[0xF] = 0x03;          // LSB = 1
  chip8_cycle(&c);
  ASSERT_EQ_FMT(1, c.registers[0xF], "%d"); // shifted-out bit, not 0x01
  PASS();
}

// 8XYE SHL into VF.
TEST shl_into_vf_keeps_flag(void) {
  chip8_t c = make_machine(0x8F0E); // SHL VF
  c.registers[0xF] = 0x81;          // MSB = 1
  chip8_cycle(&c);
  ASSERT_EQ_FMT(1, c.registers[0xF], "%d"); // shifted-out bit, not 0x02
  PASS();
}

// -----------------------------------------------------------------------------
// A couple of sanity checks on the normal (non-VF-target) paths, so the suite
// also guards against the "fixed VF but broke the math" class of regression.
// -----------------------------------------------------------------------------

TEST add_sets_carry_and_truncates(void) {
  chip8_t c = make_machine(0x8014); // ADD V0, V1
  c.registers[0] = 200;
  c.registers[1] = 100;
  chip8_cycle(&c);
  ASSERT_EQ_FMT(44, c.registers[0], "%d");  // 300 & 0xFF
  ASSERT_EQ_FMT(1, c.registers[0xF], "%d"); // carry
  PASS();
}

SUITE(arithmetic_suite) {
  RUN_TEST(sub_equal_operands_sets_no_borrow);
  RUN_TEST(sub_larger_minus_smaller_sets_no_borrow);
  RUN_TEST(sub_smaller_minus_larger_sets_borrow_and_wraps);
  RUN_TEST(subn_equal_operands_sets_no_borrow);
  RUN_TEST(subn_borrow_wraps);
  RUN_TEST(add_carry_into_vf_keeps_flag);
  RUN_TEST(add_no_carry_into_vf_keeps_flag);
  RUN_TEST(sub_into_vf_keeps_flag);
  RUN_TEST(shr_into_vf_keeps_flag);
  RUN_TEST(shl_into_vf_keeps_flag);
  RUN_TEST(add_sets_carry_and_truncates);
}

// -----------------------------------------------------------------------------
// Memory-safety regression suite.
//
// These pin the address-masking (wrap) behavior of the core so the out-of-bounds
// class of bug cannot silently return. Each test both (a) asserts the exact
// wrapped destination/source, and (b) checks a canary on the struct fields that
// the original OOB bug used to clobber (registers[], index_register,
// program_counter). Runs under plain gcc -- no sanitizer required.
// -----------------------------------------------------------------------------

// FX55 dumping all 16 registers from I = 0x0FFF must wrap into the start of RAM,
// NOT walk past memory[] into registers[]/index/PC. This is the exact exploit
// the reviewer described (up to 15 bytes past memory[4096]).
TEST fx55_full_dump_wraps_and_preserves_cpu_state(void) {
  chip8_t c = make_machine(0xFF55); // LD [I], V0..VF
  c.index_register = 0x0FFF;
  for (int i = 0; i < 16; i++)
    c.registers[i] = (uint8_t)(0x10 + i); // distinct: 0x10..0x1F

  chip8_cycle(&c);

  // Semantics: addresses wrap. mem[0xFFF]=V0, mem[(0x1000+k)&0xFFF]=V(k+1).
  ASSERT_EQ_FMT(0x10, c.memory[0x0FFF], "0x%02X"); // V0
  for (int i = 1; i < 16; i++)
    ASSERT_EQ_FMT((uint8_t)(0x10 + i), c.memory[i - 1], "0x%02X"); // V1..VF

  // Canary: the dump must not have corrupted CPU state. I is unchanged (this
  // core does not auto-increment I), PC advanced by exactly one instruction,
  // and the source registers are untouched.
  ASSERT_EQ_FMT(0x0FFF, c.index_register, "0x%04X");
  ASSERT_EQ_FMT(CHIP8_PROGRAM_START_ADDRESS + 2, c.program_counter, "0x%04X");
  for (int i = 0; i < 16; i++)
    ASSERT_EQ_FMT((uint8_t)(0x10 + i), c.registers[i], "0x%02X");
  PASS();
}

// FX65 loading registers from I = 0x0FFF must read wrapped RAM, and must not
// touch registers above the loaded range.
TEST fx65_load_wraps_and_leaves_other_registers(void) {
  chip8_t c = make_machine(0xF265); // LD V0..V2, [I]
  c.index_register = 0x0FFF;
  c.memory[0x0FFF] = 0xA0; // -> V0
  c.memory[0x000] = 0xA1;  // -> V1 (wrapped)
  c.memory[0x001] = 0xA2;  // -> V2 (wrapped)
  for (int i = 3; i < 16; i++)
    c.registers[i] = 0xCC; // canary

  chip8_cycle(&c);

  ASSERT_EQ_FMT(0xA0, c.registers[0], "0x%02X");
  ASSERT_EQ_FMT(0xA1, c.registers[1], "0x%02X");
  ASSERT_EQ_FMT(0xA2, c.registers[2], "0x%02X");
  for (int i = 3; i < 16; i++)
    ASSERT_EQ_FMT(0xCC, c.registers[i], "0x%02X"); // untouched
  ASSERT_EQ_FMT(0x0FFF, c.index_register, "0x%04X");
  PASS();
}

// FX33 BCD store at I = 0x0FFF must wrap its three bytes into RAM.
TEST fx33_bcd_wraps_at_end_of_memory(void) {
  chip8_t c = make_machine(0xF033); // LD B, V0
  c.index_register = 0x0FFF;
  c.registers[0] = 123;

  chip8_cycle(&c);

  ASSERT_EQ_FMT(1, c.memory[0x0FFF], "%d"); // hundreds
  ASSERT_EQ_FMT(2, c.memory[0x000], "%d");  // tens   (wrapped)
  ASSERT_EQ_FMT(3, c.memory[0x001], "%d");  // units  (wrapped)
  ASSERT_EQ_FMT(123, c.registers[0], "%d"); // source untouched
  ASSERT_EQ_FMT(0x0FFF, c.index_register, "0x%04X");
  PASS();
}

// DXYN sprite read must wrap when I + row crosses the end of RAM.
TEST dxyn_sprite_read_wraps_at_end_of_memory(void) {
  chip8_t c = make_machine(0xD002); // DRW V0, V0, height=2
  c.index_register = 0x0FFF;
  c.registers[0] = 0;       // draw at (0,0)
  c.memory[0x0FFF] = 0x80;  // row 0: leftmost pixel
  c.memory[0x000] = 0x80;   // row 1: read from (I+1) wrapped to 0

  chip8_cycle(&c);

  // Row 0 lights display[0]; row 1 (wrapped read) lights display[DISPLAY_WIDTH].
  const uint8_t *disp = chip8_get_display(&c);
  ASSERT_EQ_FMT(1, disp[0], "%d");
  ASSERT_EQ_FMT(1, disp[DISPLAY_WIDTH], "%d");
  ASSERT_EQ_FMT(0, c.registers[0xF], "%d"); // no collision on a clear screen
  PASS();
}

// Opcode fetch at PC = 0x0FFF must read the high byte from 0xFFF and the low
// byte from the wrapped address 0x000 -- never memory[0x1000] (OOB).
TEST pc_fetch_wraps_at_end_of_memory(void) {
  chip8_t c;
  chip8_init(&c);
  c.program_counter = 0x0FFF;
  c.memory[0x0FFF] = 0x60; // high byte: 6XNN
  c.memory[0x000] = 0x42;  // low byte (wrapped): NN = 0x42 -> opcode 0x6042

  chip8_cycle(&c); // executes LD V0, 0x42

  ASSERT_EQ_FMT(0x42, c.registers[0], "0x%02X"); // proves low byte came from [0]
  PASS();
}

// EX9E with an out-of-range key (Vx >= 16) must NOT skip, even if the aliased
// in-range key (Vx & 0x0F) is held. Guards against the masking regression.
TEST ex9e_out_of_range_key_does_not_skip(void) {
  chip8_t c = make_machine(0xE09E); // SKP V0
  c.registers[0] = 0xFF;            // 0xFF & 0x0F == 0x0F
  c.keypad[0x0F] = 1;               // the aliased key IS pressed
  uint16_t pc_before = c.program_counter;

  chip8_cycle(&c);

  // Fetch advances PC by 2; the skip must NOT add a further 2.
  ASSERT_EQ_FMT(pc_before + 2, c.program_counter, "0x%04X");
  PASS();
}

// EXA1 with an out-of-range key must skip (an absent key is "not pressed"),
// even though the aliased in-range key is held.
TEST exa1_out_of_range_key_skips(void) {
  chip8_t c = make_machine(0xE0A1); // SKNP V0
  c.registers[0] = 0xFF;
  c.keypad[0x0F] = 1;
  uint16_t pc_before = c.program_counter;

  chip8_cycle(&c);

  // Fetch (+2) plus the skip (+2).
  ASSERT_EQ_FMT(pc_before + 4, c.program_counter, "0x%04X");
  PASS();
}

// Sanity: a valid, pressed key still skips on EX9E (we did not break the normal
// path while bounds-checking).
TEST ex9e_valid_pressed_key_skips(void) {
  chip8_t c = make_machine(0xE09E); // SKP V0
  c.registers[0] = 0x05;
  c.keypad[0x05] = 1;
  uint16_t pc_before = c.program_counter;

  chip8_cycle(&c);

  ASSERT_EQ_FMT(pc_before + 4, c.program_counter, "0x%04X");
  PASS();
}

// FX29 must mask Vx to a nibble: I = font_start + (Vx & 0x0F) * 5.
TEST fx29_masks_vx_to_nibble(void) {
  chip8_t c = make_machine(0xF029); // LD F, V0
  c.registers[0] = 0x1A;            // nibble 0x0A -> glyph 'A'

  chip8_cycle(&c);

  // 0x50 + (0x0A * 5) == 0x50 + 50 == 130 (0x82). Unmasked would be 0xD2.
  ASSERT_EQ_FMT(CHIP8_FONT_START_ADDRESS + (0x0A * 5), c.index_register,
                "0x%04X");
  PASS();
}

SUITE(memory_safety_suite) {
  RUN_TEST(fx55_full_dump_wraps_and_preserves_cpu_state);
  RUN_TEST(fx65_load_wraps_and_leaves_other_registers);
  RUN_TEST(fx33_bcd_wraps_at_end_of_memory);
  RUN_TEST(dxyn_sprite_read_wraps_at_end_of_memory);
  RUN_TEST(pc_fetch_wraps_at_end_of_memory);
  RUN_TEST(ex9e_out_of_range_key_does_not_skip);
  RUN_TEST(exa1_out_of_range_key_skips);
  RUN_TEST(ex9e_valid_pressed_key_skips);
  RUN_TEST(fx29_masks_vx_to_nibble);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(arithmetic_suite);
  RUN_SUITE(memory_safety_suite);
  GREATEST_MAIN_END();
}
