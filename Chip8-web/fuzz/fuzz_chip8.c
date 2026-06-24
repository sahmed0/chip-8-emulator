#include "../chip8.h"
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_ROM_SIZE (CHIP8_MEMORY_SIZE - CHIP8_PROGRAM_START_ADDRESS)
/* Cap cycles to prevent infinite-loop ROMs (e.g. 1NNN jumping to itself)
   from hanging the fuzzer. 5000 cycles is enough to reach all OOB paths. */
#define CYCLE_CAP 5000

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

    chip8_init(chip8);
    /* chip8_init seeds rand() with time(NULL); override it so the CXNN opcode
       is deterministic and any crash reproduces on corpus replay. */
    srand(0);

    /* Seed initial key/register state from the input so the fuzzer can reach
       the keypad branches (EX9E/EXA1/FX0A) instead of always spinning on the
       "no key pressed" path. */
    if (size > 0)
        chip8->keypad[data[0] & 0x0F] = 1;

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
