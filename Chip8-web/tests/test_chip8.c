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

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(arithmetic_suite);
  GREATEST_MAIN_END();
}
