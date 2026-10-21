// neogeo_nav.h - USB4NeoGeo Navigation & UX System
// Retro Frog USB4NeoGeo
//
// Two LED color modes:
//   Green  = Pad layouts (Pad A, Pad B)
//   Purple = Custom controls (user-defined remap, blank by default)
//   Blue   = Mouse Joystick mode (auto-detected on mouse connect)
//   Yellow = Mouse Spinner/Paddle mode (auto-detected on mouse connect)
//
// Navigation: Hold Select 2 seconds to enter navigation mode.
//   Select is suppressed from Neo Geo output for the entire hold duration.
//   Up/Down   = cycle profiles within current mode
//   Left/Right= switch between Green and Purple modes
//   Start     = (Purple mode only) begin remap sequence
//   Release   = save state to flash and exit navigation mode
//
// Turbo: Hold Select 2s -> tap a face button to toggle 10Hz turbo.
//   Turbo cleared on any mode/profile switch.
//   Turbo NOT saved to flash.
//   Double blink LED every 3 seconds while turbo is active.
//
// Mouse modes: Auto-detected when USB mouse connects.
//   BOOTSEL button toggles between Joystick and Spinner mode.
//   DPI: Hold middle mouse 2s -> DPI adjust mode.
//     Left button = decrease divisor (faster)
//     Right button = increase divisor (slower)
//     Tap middle mouse = exit and save

#ifndef NEOGEO_NAV_H
#define NEOGEO_NAV_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_event.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

#define NAV_HOLD_MS             2000
#define NAV_SELECT_SUPPRESS_MS  1500
#define NAV_TURBO_HZ            15
#define NAV_TURBO_PERIOD_MS     100
#define NAV_TURBO_HALF_MS       33
#define NAV_TURBO_BLINK_MS      3000
#define NAV_REMAP_SLOTS         6

// Profile indices
#define NAV_PAD_START           5
#define NAV_PAD_COUNT           2
#define NAV_BLANK_INDEX         7

// Flash slots
#define NAV_FLASH_SLOT          3   // nav state
#define NAV_MOUSE_FLASH_SLOT    2   // mouse mode + DPI

// DPI system
#define NAV_DPI_MIN             1
#define NAV_DPI_MAX             8
#define NAV_DPI_DEFAULT         2
#define NAV_DPI_ADJUST_HOLD_MS  2000
#define NAV_DPI_DEBOUNCE_MS     300

// Spinner counter
#define NAV_SPINNER_MAX         127
#define NAV_SPINNER_MIN         0

// ============================================================================
// TYPES
// ============================================================================

typedef enum {
    NAV_MODE_PAD    = 0,   // Green LED
    NAV_MODE_CUSTOM = 1,   // Purple LED
} nav_color_mode_t;

typedef enum {
    NAV_MOUSE_JOYSTICK = 0,  // Blue LED
    NAV_MOUSE_SPINNER  = 1,  // Yellow LED
} nav_mouse_mode_t;

typedef enum {
    NAV_STATE_NORMAL = 0,
    NAV_STATE_NAV    = 1,
    NAV_STATE_REMAP  = 2,
} nav_state_t;

// ============================================================================
// PUBLIC API
// ============================================================================

void     neogeo_nav_init(void);
void     neogeo_nav_task(void);
void     neogeo_nav_on_connect(bool connected);

// Call from gpio_tap_callback with raw buttons (controller)
uint32_t neogeo_nav_filter_buttons(uint32_t raw_buttons);

// Call from gpio_tap_callback for mouse events
void     neogeo_nav_handle_mouse(const input_event_t* event);

// Returns true when controller output should be suppressed
bool     neogeo_nav_is_suppressed(void);

// Turbo API
uint32_t neogeo_nav_get_turbo_mask(void);
uint32_t neogeo_nav_get_turbo_pin_mask(void);
bool     neogeo_nav_turbo_phase_on(void);
uint32_t neogeo_nav_get_turbo_gpio_mask(const void* port, uint32_t last_gpio_buttons);

// Returns true if a mouse is currently connected
bool     neogeo_nav_mouse_connected(void);

// Call when a mouse connects or disconnects
void     neogeo_nav_set_mouse_connected(bool connected);

#endif // NEOGEO_NAV_H
