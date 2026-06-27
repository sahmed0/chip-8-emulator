@echo off
echo Building Chip-8 for Web...
call emcc -O3 ^
    main.c chip8.c rewind.c ^
    -s WASM=1 ^
    -s USE_SDL=2 ^
    --preload-file roms ^
    -s "EXPORTED_FUNCTIONS=['_main', '_malloc', '_free', '_get_memory_buffer_ptr', '_get_register_v_value', '_get_program_counter', '_get_index_register', '_get_delay_timer_value', '_get_sound_timer_value', '_load_rom_from_file', '_set_emulator_paused', '_get_is_paused', '_step_single_cycle', '_set_rewinding', '_reset_emulator_state', '_get_available_theme_count', '_get_theme_name_at_index', '_set_active_theme']" ^
    -s "EXPORTED_RUNTIME_METHODS=['ccall', 'cwrap', 'FS', 'UTF8ToString']" ^
    -o ..\docs\chip8.js

echo Copying favicon...
copy /Y ..\vite.svg ..\docs\


if %errorlevel% neq 0 (
    echo Build failed!
    exit /b %errorlevel%
)

echo Build successful! Output in ..\docs\index.html
pause
