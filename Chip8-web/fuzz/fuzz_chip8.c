#include "../chip8.h"
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_ROM_SIZE (CHIP8_MEMORY_SIZE - CHIP8_PROGRAM_START_ADDRESS)
/* Cap cycles to prevent infinite-loop ROMs (e.g. 1NNN jumping to itself)
   from hanging the fuzzer. After fixing initial few crashes, fuzzer kept running for too long at 5000 cycles, so 1000 cycles should be enough to reach all OOB paths in a 3.5 KB ROM. */
#define CYCLE_CAP 1000

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > MAX_ROM_SIZE)
        size = MAX_ROM_SIZE;

    /* Heap-allocate the context so ASan surrounds the whole chip8_t with
       redzones. memory[] sits near the start of the struct, so far out-of-
       bounds accesses (PC wrapping the uint16_t, large I + offset) are caught.
       NOTE: ASan does not detect overflow *between* members of the same
       allocation, so OOB into memory[] that stays inside the struct still
       slips past; UBSan catches the integer-overflow cases. */
    chip8_t *chip8 = malloc(sizeof *chip8);
    if (!chip8)
        return 0;

    /* Fixed nonzero seed => the CXNN opcode is deterministic and any crash
       reproduces on corpus replay. (The core no longer touches global rand().) */
    chip8_init(chip8, 0xC8C8C8C8u);

    /* Fuzz both dialects: byte 2 selects the quirk profile. */
    if (size > 2 && (data[2] & 1))
        chip8_set_quirks(chip8, chip8_quirks_schip());

    /* Seed initial key/register state from the input so the fuzzer can reach
       the keypad branches (EX9E/EXA1/FX0A) instead of always spinning on the
       "no key pressed" path. */
    // Press two keys derived from both nibbles of first byte,
    // and a third from the second byte if available; maximises the chance
    // that FX0A finds a match without pressing every key unconditionally
    if (size > 0) {
        chip8->keypad[data[0] & 0x0F] = 1;
        chip8->keypad[(data[0] >> 4) & 0x0F] = 1;
    }
    if (size > 1) {
        chip8->keypad[data[1] & 0x0F] = 1;
    }

    chip8_load_rom(chip8, data, size);

    for (int i = 0; i < CYCLE_CAP; i++) {
        chip8_cycle(chip8);
        /* Exercise the timer decrement path at a roughly 60Hz cadence. */
        if ((i & 0x07) == 0)
            chip8_update_timers(chip8);
    }

    free(chip8);
    return 0;
}
