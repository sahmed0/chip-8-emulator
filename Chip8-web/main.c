#include "chip8.h" // Defines the Chip-8 system structure and function prototypes.
#include "logging.h" // Provides the LOG_INFO, LOG_ERROR macros for console output.
#include <SDL.h> // Simple DirectMedia Layer: Handles window, rendering, and audio.
#include <stdbool.h> // Standard bool support (true/false).
#include <stdio.h>   // Standard I/O (printf, fopen, etc.).
#include <stdlib.h>  // Standard Library (malloc, free, rand).
#include <string.h>  // String helpers (strdup) for the native window title.

#ifdef __EMSCRIPTEN__
#include <emscripten.h> // Emscripten headers for WASM integration (main loop, keepalive).
#else
#define EMSCRIPTEN_KEEPALIVE // Expand to nothing on non-Emscripten builds
#endif

// -----------------------------------------------------------------------------
// Emulator Configuration
// -----------------------------------------------------------------------------

// Screen dimensions (scaled up 10x for visibility)
// The original Chip-8 resolution is 64x32.
#define WINDOW_WIDTH (DISPLAY_WIDTH * 10)
#define WINDOW_HEIGHT (DISPLAY_HEIGHT * 10)

// Audio settings for the square wave beep
#define AUDIO_SAMPLE_RATE 44100 // Standard CD Quality sample rate
#define AUDIO_AMPLITUDE 3000    // Volume level (approx 10% of max)
#define AUDIO_FREQUENCY 440     // Pitch of the beep (A4 note)

/**
 * Audio Callback Function.
 * Generates a simple square wave for sound output.
 * Called by SDL whenever the audio device needs more data.
 *
 * @param userdata User defined data (unused).
 * @param stream The audio buffer to fill.
 * @param len The length of the audio buffer in bytes.
 */
void generate_audio_wave(void *userdata, uint8_t *stream, int len) {
  int16_t *buffer = (int16_t *)stream;
  static uint32_t running_sample_index = 0;
  int square_wave_period = AUDIO_SAMPLE_RATE / AUDIO_FREQUENCY;
  int half_period = square_wave_period / 2;

  // Fill the audio buffer with a square wave
  for (int i = 0; i < len / 2; i++) {
    buffer[i] = ((running_sample_index++ / half_period) % 2) ? AUDIO_AMPLITUDE
                                                             : -AUDIO_AMPLITUDE;
  }
}

// -----------------------------------------------------------------------------
// Theme System
// -----------------------------------------------------------------------------
// The emulator supports multiple colour themes. These are defined here in C
// and exposed to the Web Frontend via Emscripten bindings.

typedef struct {
  uint32_t foreground_color_rgba; // Colour for active pixels (RGBA8888)
  uint32_t background_color_rgba; // Colour for inactive pixels (RGBA8888)
  const char *theme_name;         // Display name shown in the UI dropdown
} EmulatorTheme;

EmulatorTheme g_color_themes[] = {
    {0xFFFFFFFF, 0x00000000, "Classic"},  // Simple B&W
    {0x00F3FFFF, 0x050520FF, "Blue"},     // Cyberpunk Blue (Blade Runner style)
    {0xFF00FFFF, 0x100010FF, "Pink"},     // Neon Magenta
    {0x00FF00FF, 0x00000000, "Green"},    // Retro Phosphor Green
    {0xFFB000FF, 0x200000FF, "Amber"},    // Vintage Amber Monitor
    {0x00000000, 0xFFFFFFFF, "Inverted"}, // Paper mode (Black on White)
};

int g_current_theme_index = 0; // Index of the currently active theme
int g_total_themes = sizeof(g_color_themes) / sizeof(g_color_themes[0]);

// -----------------------------------------------------------------------------
// Global State
// -----------------------------------------------------------------------------
// SDL resources must be global so they can be accessed by the main loop
// and cleanup functions.
SDL_Window *g_sdl_window = NULL; // The window handle
SDL_Renderer *g_sdl_renderer =
    NULL; // The rendering context (hardware accelerated)
SDL_Texture *g_sdl_texture =
    NULL; // The texture where pixels are drawn before rendering
chip8_t
    g_chip8_core; // The core Chip-8 emulator instance (cpu state, memory, etc.)

// Global ROM buffer for reset functionality.
// When a ROM is loaded from the web, we save a copy here so the user can
// "Reset" the game without re-uploading the file.
uint8_t *g_backup_rom_buffer = NULL;
long g_backup_rom_size = 0;
bool g_is_paused = false; // Controls the pause state from the Web UI

// -----------------------------------------------------------------------------
// Timing Constants
// -----------------------------------------------------------------------------
// Chip-8 programs expect to run at approx 60Hz.
// Since Emscripten's main_tick runs at the browser's refresh rate (usually
// 60Hz), we can execute one frame's worth of CPU cycles per tick.
const int TARGET_FPS = 60;
const int DESIRED_FRAME_DELAY_MS = 1000 / 60;
const int CPU_CYCLES_PER_FRAME =
    10; // Number of CPU instructions to execute per frame (Speed)

// -----------------------------------------------------------------------------
// Web Assembly Interface (Emscripten Exports)
// -----------------------------------------------------------------------------
// These functions are exported to JavaScript so the web UI can interact with
// the C code. They allow the JS frontend to read registers, control execution,
// and load ROMs.

#ifdef __EMSCRIPTEN__
/**
 * Get a pointer to the emulator's raw memory buffer.
 * Used by JS to visualize RAM contents.
 * @return Pointer to the 4096-byte memory array.
 */
EMSCRIPTEN_KEEPALIVE uint8_t *get_memory_buffer_ptr() {
  return g_chip8_core.memory;
}

/**
 * Get the value of a specific V-register.
 *
 * @param index The register index (0-15).
 * @return The 8-bit value of the register.
 */
EMSCRIPTEN_KEEPALIVE
uint8_t get_register_v_value(int index) {
  if (index >= 0 && index < 16)
    return g_chip8_core.registers[index];
  return 0;
}

/**
 * Get the current Program Counter (PC).
 * @return The 16-bit PC value.
 */
EMSCRIPTEN_KEEPALIVE
uint16_t get_program_counter() { return g_chip8_core.program_counter; }

/**
 * Get the current Index Register (I).
 * @return The 16-bit Index Register value.
 */
EMSCRIPTEN_KEEPALIVE
uint16_t get_index_register() { return g_chip8_core.index_register; }

/**
 * Get the current Delay Timer value.
 * @return The 8-bit Delay Timer value.
 */
EMSCRIPTEN_KEEPALIVE
uint8_t get_delay_timer_value() { return g_chip8_core.delay_timer; }

/**
 * Get the current Sound Timer value.
 * @return The 8-bit Sound Timer value.
 */
EMSCRIPTEN_KEEPALIVE
uint8_t get_sound_timer_value() { return g_chip8_core.sound_timer; }
#endif

/**
 * Internal helper to finalise ROM loading.
 * Creates a backup of the ROM data and initializes the core.
 *
 * @param buffer Pointer to the ROM data.
 * @param size Size of the ROM data in bytes.
 */
static void load_rom_data(uint8_t *buffer, int size) {
  // Free previous backup if it exists to avoid memory leaks
  if (g_backup_rom_buffer)
    free(g_backup_rom_buffer);

  // Allocate new backup buffer
  g_backup_rom_size = size;
  g_backup_rom_buffer = (uint8_t *)malloc(size);
  if (g_backup_rom_buffer)
    memcpy(g_backup_rom_buffer, buffer, size); // Save a copy for reset

  // Initialise the Chip-8 state (clear memory, registers, stack)
  chip8_init(&g_chip8_core);
  // Load the ROM into memory starting at 0x200
  chip8_load_rom(&g_chip8_core, buffer, size);
  printf("Web: Loaded ROM, size %d\n", size);
}

/**
 * Load a ROM from a file path using standard IO.
 * This is used by the frontend to load built-in ROMs directly from the
 * Emscripten Virtual File System (VFS) or standard file system.
 *
 * @param filename The path to the ROM file.
 * @return true if successful, false otherwise.
 */
EMSCRIPTEN_KEEPALIVE
bool load_rom_from_file(const char *filename) {
  FILE *rom_file = fopen(filename, "rb");
  if (rom_file) {
    fseek(rom_file, 0, SEEK_END);
    long rom_size = ftell(rom_file);
    rewind(rom_file);

    uint8_t *rom_buffer = (uint8_t *)malloc(rom_size);
    if (rom_buffer) {
      fread(rom_buffer, 1, rom_size, rom_file);
      load_rom_data(rom_buffer, rom_size); // Handles init and backup
      free(rom_buffer);
      LOG_INFO("Loaded ROM from file: %s (%ld bytes)", filename, rom_size);
      fclose(rom_file);
      return true;
    } else {
      LOG_ERROR("Failed to allocate memory for ROM");
    }
    fclose(rom_file);
  } else {
    LOG_WARN("Could not open ROM file: %s", filename);
  }
  return false;
}

/**
 * Resets the emulator to its initial state and reloads the current ROM.
 * Triggered by the "Reset" button in the UI.
 */
EMSCRIPTEN_KEEPALIVE
void reset_emulator_state() {
  chip8_init(&g_chip8_core);
  if (g_backup_rom_buffer && g_backup_rom_size > 0) {
    chip8_load_rom(&g_chip8_core, g_backup_rom_buffer, g_backup_rom_size);
  }
}

#ifdef __EMSCRIPTEN__
/**
 * Set the pause state of the emulator.
 *
 * @param paused true to pause, false to resume.
 */
EMSCRIPTEN_KEEPALIVE
void set_emulator_paused(bool paused) { g_is_paused = paused; }

/**
 * Get the current pause state.
 * @return true if paused, false otherwise.
 */
EMSCRIPTEN_KEEPALIVE
bool get_is_paused() { return g_is_paused; }

/**
 * Executes a single CPU cycle while paused.
 * Allows the user to step through instructions one by one.
 */
EMSCRIPTEN_KEEPALIVE
void step_single_cycle() {
  if (g_is_paused) {
    chip8_cycle(&g_chip8_core);
  }
}

// -----------------------------------------------------------------------------
// Theme API
// -----------------------------------------------------------------------------

/**
 * Get the total number of available colour themes.
 * @return The count of themes.
 */
EMSCRIPTEN_KEEPALIVE
int get_available_theme_count() { return g_total_themes; }

/**
 * Get the name of a theme at a specific index.
 *
 * @param index Theme index.
 * @return The name string (e.g., "Classic", "Cyberpunk").
 */
EMSCRIPTEN_KEEPALIVE
const char *get_theme_name_at_index(int index) {
  if (index >= 0 && index < g_total_themes) {
    return g_color_themes[index].theme_name;
  }
  return "";
}

/**
 * Set the active colour theme.
 * Forces a redraw to apply the new colours immediately.
 *
 * @param index Theme index.
 */
EMSCRIPTEN_KEEPALIVE
void set_active_theme(int index) {
  if (index >= 0 && index < g_total_themes) {
    g_current_theme_index = index;
    g_chip8_core.draw_flag =
        true; // Force a redraw so the new colors apply immediately
  }
}
#endif

// -----------------------------------------------------------------------------
// Main Loop
// -----------------------------------------------------------------------------

/**
 * The main emulator loop tick.
 * Handles Input, Physics (Timers), CPU Cycles, and Rendering.
 * Called by Emscripten's main loop mechanism or the while(1) loop on desktop.
 */
void emulator_main_loop(void) {
  // If paused, we might still want to process input/UI events to prevent the
  // window from freezing, but we skip the CPU cycle execution.
  if (g_is_paused) {
    // Intentionally empty: Logic handles pause by skipping the cpu cycle block
    // below.
  }

  uint32_t frame_start_time = SDL_GetTicks();
  SDL_Event event;

  // Event Handling
  while (SDL_PollEvent(&event) != 0) {
    if (event.type == SDL_QUIT) {
#ifndef __EMSCRIPTEN__
      exit(0); // Exit immediately on desktop
#endif
    } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
      bool key_is_pressed = (event.type == SDL_KEYDOWN);
      int chip8_key = -1;

      // Map SDL Keys to Chip-8 Keypad (Hex Layout)
      switch (event.key.keysym.sym) {
      case SDLK_1:
        chip8_key = 0x1;
        break;
      case SDLK_2:
        chip8_key = 0x2;
        break;
      case SDLK_3:
        chip8_key = 0x3;
        break;
      case SDLK_4:
        chip8_key = 0xC;
        break;

      case SDLK_q:
        chip8_key = 0x4;
        break;
      case SDLK_w:
        chip8_key = 0x5;
        break;
      case SDLK_e:
        chip8_key = 0x6;
        break;
      case SDLK_r:
        chip8_key = 0xD;
        break;

      case SDLK_a:
        chip8_key = 0x7;
        break;
      case SDLK_s:
        chip8_key = 0x8;
        break;
      case SDLK_d:
        chip8_key = 0x9;
        break;
      case SDLK_f:
        chip8_key = 0xE;
        break;

      case SDLK_z:
        chip8_key = 0xA;
        break;
      case SDLK_x:
        chip8_key = 0x0;
        break;
      case SDLK_c:
        chip8_key = 0xB;
        break;
      case SDLK_v:
        chip8_key = 0xF;
        break;

      case SDLK_ESCAPE:
#ifndef __EMSCRIPTEN__
        exit(0); // Helper to quit easily on desktop
#endif
        break;

      case SDLK_F1:
        // Cycle Colour Theme on F1 press (Desktop debugging mostly)
        // Web UI uses a dropdown instead.
        if (event.type == SDL_KEYDOWN) {
          g_current_theme_index = (g_current_theme_index + 1) % g_total_themes;
          char title_buffer[256];
          snprintf(title_buffer, sizeof(title_buffer),
                   "Chip-8 Emulator - Theme: %s",
                   g_color_themes[g_current_theme_index].theme_name);
          SDL_SetWindowTitle(g_sdl_window, title_buffer);

          g_chip8_core.draw_flag = true;
        }
        break;

      // --- New Control Shortcuts ---
      case SDLK_SPACE:
        if (event.type == SDL_KEYDOWN) {
          g_is_paused = !g_is_paused;
          // Sync with JS if needed (optional, JS polls status or we push it?
          // For now, C handles the logic, JS button might get out of sync
          // visually unless we call a JS function, but let's stick to C logic
          // first. Actually, better to expose a way for JS to know, or just let
          // C handle it. The JS 'updateRegisters' won't know it's paused unless
          // we export a getter. We'll rely on visual feedback (screen stops)
          // for now. Note: The JS 'Pause' button text won't update
          // automatically with this approach. Users requested "map buttons to
          // keyboard", so functional parity is key.
        }
        break;

      case SDLK_RIGHT:
        if (event.type == SDL_KEYDOWN && g_is_paused) {
          chip8_cycle(&g_chip8_core);
          g_chip8_core.draw_flag = true; // Force redraw to show step result
        }
        break;

      case SDLK_LEFT:
        if (event.type == SDL_KEYDOWN) {
          reset_emulator_state();
          g_is_paused = false; // Auto-resume on reset
        }
        break;
      }

      if (chip8_key != -1) {
        chip8_set_key(&g_chip8_core, chip8_key, key_is_pressed);
      }
    }
  }

  // --- CPU EXECUTION ---
  // Run multiple CPU cycles per frame to speed up emulation.
  // Standard is often around 10 cycles/frame for 600Hz effective clock speed.
  if (!g_is_paused) {
    for (int i = 0; i < CPU_CYCLES_PER_FRAME; ++i) {
      chip8_cycle(&g_chip8_core);
    }
    // Update Timers at 60Hz rate
    chip8_update_timers(&g_chip8_core);
  }

  // --- AUDIO HANDLING ---
  // If the sound timer is active, tell SDL to play audio.
  if (g_chip8_core.sound_timer > 0) {
    SDL_PauseAudio(0); // Unpause (Play)
  } else {
    SDL_PauseAudio(1); // Pause
  }

  // --- RENDERING ---
  // Only redraw if the screen state has changed (draw_flag is set).
  if (g_chip8_core.draw_flag) {
    g_chip8_core.draw_flag = false;

    // Convert Chip-8 display buffer (1 bit per pixel) to SDL Texture (RGBA8888)
    // using the currently selected color theme.
    uint32_t pixel_buffer[DISPLAY_SIZE];
    const uint8_t *display_data = chip8_get_display(&g_chip8_core);

    for (int i = 0; i < DISPLAY_SIZE; i++) {
      pixel_buffer[i] =
          (display_data[i] != 0)
              ? g_color_themes[g_current_theme_index].foreground_color_rgba
              : g_color_themes[g_current_theme_index].background_color_rgba;
    }

    // Upload pixels to GPU texture
    SDL_UpdateTexture(g_sdl_texture, NULL, pixel_buffer,
                      DISPLAY_WIDTH * sizeof(uint32_t));
  }

  // Clear screen, copy texture, and present
  SDL_RenderClear(g_sdl_renderer);
  SDL_RenderCopy(g_sdl_renderer, g_sdl_texture, NULL, NULL);
  SDL_RenderPresent(g_sdl_renderer);

#ifndef __EMSCRIPTEN__
  // --- FRAME RATE CAPPING (Desktop Only) ---
  // Emscripten handles this via requestAnimationFrame, but on desktop we need
  // to sleep.
  uint32_t frame_duration = SDL_GetTicks() - frame_start_time;
  if (frame_duration < DESIRED_FRAME_DELAY_MS) {
    SDL_Delay(DESIRED_FRAME_DELAY_MS - frame_duration);
  }
#endif
}

// -----------------------------------------------------------------------------
// Entry Point
// -----------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  // Initialise SDL (Video and Audio subsystems)
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
    LOG_ERROR("SDL could not initialise! SDL_Error: %s", SDL_GetError());
    return 1;
  }

  // Fetch the title dynamically from the HTML document using Emscripten JS
  // interop. On desktop there is no document, so fall back to a static title.
  // Either branch yields a heap-allocated string freed below.
#ifdef __EMSCRIPTEN__
  char *window_title = (char *)EM_ASM_INT({
    var title = document.title;
    var buffer = _malloc(lengthBytesUTF8(title) + 1);
    stringToUTF8(title, buffer, lengthBytesUTF8(title) + 1);
    return buffer;
  });
#else
  char *window_title = strdup("Chip-8 Emulator");
#endif

  // Create the emulator window
  g_sdl_window = SDL_CreateWindow(
      window_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

  free(window_title); // Free the string allocated in JS

  if (g_sdl_window == NULL) {
    LOG_ERROR("Window could not be created! SDL_Error: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  // Create Hardware Accelerated Renderer
  g_sdl_renderer =
      SDL_CreateRenderer(g_sdl_window, -1, SDL_RENDERER_ACCELERATED);

  // Set logical size for automatic scaling (keeps the 64x32 aspect ratio)
  SDL_RenderSetLogicalSize(g_sdl_renderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);

  // Create a streaming texture (allocates VRAM for the screen)
  g_sdl_texture = SDL_CreateTexture(g_sdl_renderer, SDL_PIXELFORMAT_RGBA8888,
                                    SDL_TEXTUREACCESS_STREAMING, DISPLAY_WIDTH,
                                    DISPLAY_HEIGHT);

  // Initialise Audio
  SDL_AudioSpec desired_audio_spec, obtained_audio_spec;
  SDL_zero(desired_audio_spec);
  desired_audio_spec.freq = AUDIO_SAMPLE_RATE;
  desired_audio_spec.format = AUDIO_S16SYS;
  desired_audio_spec.channels = 1;   // Mono sound
  desired_audio_spec.samples = 2048; // Buffer size
  desired_audio_spec.callback = generate_audio_wave;

  if (SDL_OpenAudio(&desired_audio_spec, &obtained_audio_spec) < 0) {
    LOG_ERROR("Failed to open audio: %s", SDL_GetError());
  } else {
    // Start audio (paused initially, controlled by sound timer)
    SDL_PauseAudio(1);
  }

  // Initialise Chip-8 Context (Memory, CPU)
  if (!chip8_init(&g_chip8_core)) {
    LOG_ERROR("Failed to initialise Chip-8");
    return 1;
  }
  LOG_INFO("Chip-8 Initialised");

  // Load Initial ROM File
  // Defaults to Space Invaders if no argument is provided
  // Load Initial ROM File
  // Defaults to Space Invaders if no argument is provided
  const char *rom_filename = (argc > 1) ? argv[1] : "/roms/Space_Invaders.ch8";
  if (!load_rom_from_file(rom_filename)) {
    LOG_WARN("Failed to load initial ROM: %s", rom_filename);
  }

  // --- START MAIN LOOP ---
#ifdef __EMSCRIPTEN__
  // Emscripten requires a special main loop function that doesn't block the
  // browser.
  // - emulator_main_loop: the function to call every frame
  // - 0: fps (0 = use requestAnimationFrame)
  // - 1: simulate infinite loop (stops this function from returning)
  emscripten_set_main_loop(emulator_main_loop, 0, 1);
#else
  // Desktop Infinite Loop
  while (1) {
    emulator_main_loop();
  }

  // Cleanup code (technically unreachable on desktop due to while(1) but good
  // practice)
  SDL_DestroyTexture(g_sdl_texture);
  SDL_DestroyRenderer(g_sdl_renderer);
  SDL_DestroyWindow(g_sdl_window);
  SDL_CloseAudio();
  SDL_Quit();
  LOG_INFO("Exiting Emulator...");
#endif

  return 0;
}