// jaguar_device.h - Atari Jaguar HD15 output driver for JoypadOS
//
// Supports these input modes routed to a single HD15 output:
//   USB Gamepad  — standard joypad matrix scan response
//   USB Mouse    — digital spinner (quadrature phase) + ROTARY type ID
//                  for Tempest 2000 and compatible titles
//   USB Keyboard — ASCII keyboard protocol (JagNote2-compatible)
//
// Input mode is detected automatically from the connected USB device type.
//
// Architecture:
//   Core 0 — USB host polling, input event tap, precomputes per-row GPIO masks
//   Core 1 — tight loop: reads all strobe pins in one GPIO register read,
//             detects transitions via XOR, writes all output pins atomically
//             via sio_hw->gpio_set / sio_hw->gpio_clr on strobe edge
//
// HD15 pin mapping:
//   Pin 1=J3, 2=J2, 3=J1, 4=J0 (strobe inputs, active LOW)
//   Pin 6=B0  (Pause / C2 / C3 type ID)
//   Pin 7=+5V, 9=GND (power only, no GPIO)
//   Pin 10=B1 (Fire A/B/C/Option — row-gated)
//   Pin 11=J11 (Right / Phase 1), 12=J10 (Left / Phase 0)
//   Pin 13=J9 (Down / keypad), 14=J8 (Up / keypad)
//   Pins 5, 8, 15 — no connect
//
// Electrical:
//   Outputs drive LOW to assert (open-drain style vs Jaguar pull-ups)
//   Inputs from Jaguar are 5V TTL

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "core/output_interface.h"

// ============================================================================
// PIN DEFINITIONS — set via CMakeLists target_compile_definitions
// ============================================================================

// Strobe inputs (Jaguar → adapter, active LOW)
#ifndef JAG_PIN_J0
#define JAG_PIN_J0   2
#endif
#ifndef JAG_PIN_J1
#define JAG_PIN_J1   3
#endif
#ifndef JAG_PIN_J2
#define JAG_PIN_J2   4
#endif
#ifndef JAG_PIN_J3
#define JAG_PIN_J3   5
#endif

// Data outputs (adapter → Jaguar, LOW = asserted)
#ifndef JAG_PIN_B0
#define JAG_PIN_B0   6   // Pause / C2 / C3 type ID
#endif
#ifndef JAG_PIN_B1
#define JAG_PIN_B1   7   // Fire A / Fire B / Fire C / Option
#endif
#ifndef JAG_PIN_J8
#define JAG_PIN_J8   8   // Up / keypad row
#endif
#ifndef JAG_PIN_J9
#define JAG_PIN_J9   9   // Down / keypad row
#endif
#ifndef JAG_PIN_J10
#define JAG_PIN_J10  10  // Left / Phase 0 (spinner)
#endif
#ifndef JAG_PIN_J11
#define JAG_PIN_J11  11  // Right / Phase 1 (spinner)
#endif

// ============================================================================
// SPINNER SENSITIVITY
// ============================================================================

// Mouse X delta divisor for spinner mode.
// 1 = 1:1 (fastest), higher values slow the spinner down.
// Adjustable at runtime via BOOTSEL hold.
#ifndef JAG_SPINNER_DIVISOR_DEFAULT
#define JAG_SPINNER_DIVISOR_DEFAULT  2
#endif
#define JAG_SPINNER_DIVISOR_MIN      1
#define JAG_SPINNER_DIVISOR_MAX      8

// ============================================================================
// INPUT MODE
// ============================================================================

typedef enum {
    JAG_MODE_GAMEPAD  = 0,  // USB gamepad  → standard joypad matrix
    JAG_MODE_SPINNER  = 1,  // USB mouse    → digital spinner + ROTARY type ID
    JAG_MODE_MOUSE    = 2,  // USB mouse    → ST/Amiga quadrature mouse protocol
    JAG_MODE_KEYBOARD = 3,  // USB keyboard → JagNote2-compatible ASCII keyboard protocol
} jag_input_mode_t;

// ============================================================================
// KEYBOARD PROTOCOL (JagNote2-compatible, port 1)
//
// Reverse-engineered from JAGNOTE2.J64 — see project doc
// usb4jag-keyboard-protocol.md. The Jaguar writes a 4-bit row code to
// J3..J0 (raw levels, J0 = bit 0) and reads B0/B1/J8-J11 back:
//
//   0100  STATUS   B0 LOW = a key is waiting, B0 HIGH = no key
//   0101  DATA_LO  key bits 0-5 on B0, B1, J11, J10, J9, J8 (LOW = 1)
//   0110  DATA_HI  key bits 6-7 on B0, B1                  (LOW = 1)
//   1000  ACK      key consumed — adapter pops its FIFO
//
// Key bytes are ASCII (US layout translated on the adapter), plus
// JagNote2 control codes: $08 BS, $0D Enter, $0E/$0F/$10/$11 cursor
// up/down/left/right.
//
// Standard pad rows (1110/1101/1011/0111) answer "nothing pressed" in
// keyboard mode, so the port still identifies as a standard pad.
// ============================================================================

#define JAG_KB_CODE_STATUS   0x4
#define JAG_KB_CODE_DATA_LO  0x5
#define JAG_KB_CODE_DATA_HI  0x6
#define JAG_KB_CODE_ACK      0x8

// Single-producer / single-consumer key FIFO.
// Core 0 (producer) owns jag_kb_head; Core 1 (consumer) owns jag_kb_tail.
#define JAG_KB_FIFO_SIZE     32     // must be a power of two
extern volatile uint8_t jag_kb_fifo[JAG_KB_FIFO_SIZE];
extern volatile uint8_t jag_kb_head;
extern volatile uint8_t jag_kb_tail;

// Typematic repeat (USB keyboards do not repeat on their own)
#define JAG_KB_REPEAT_DELAY_MS  500
#define JAG_KB_REPEAT_RATE_MS    80

// ============================================================================
// PHASE TABLE
// 3-state safe quadrature sequence (skips both-HIGH state that triggers stopclaw)
// seq[0]=state0, seq[1]=state1, seq[2]=state3 from conseq 0,1,3,2
// ============================================================================

// ============================================================================
// SHARED STATE (Core 0 writes, Core 1 reads — all volatile)
// ============================================================================

// Precomputed per-row GPIO SET masks.
// Each entry is a 32-bit bitmask: pins present = HIGH (released),
// pins absent = LOW (asserted) via gpio_clr.
// Core 0 updates these from the tap callback; Core 1 applies them atomically.
extern volatile uint32_t jag_row_gpio[4];

// 16-entry strobe lookup table for Core 1 hot path.
// Indexed by 4 strobe bits from gpio_in >> STROBE_SHIFT.
// Rebuilt by Core 0 via build_strobe_table() on every button state change.
extern volatile uint32_t jag_strobe_table[16];

// Legacy byte array kept for compatibility — not used by Core 1 hot path
extern volatile uint8_t jag_row_data[4];

// Phase sequence index (0-2, into 3-state safe sequence)
extern volatile uint8_t jag_phase_idx;    // X axis (J10/J11)
extern volatile uint8_t jag_phase_y_idx;  // Y axis (J8/J9) — mouse mode only

// Pending phase steps: Core 0 accumulates, Core 1 drains one per interval
// Positive = CW, negative = CCW
extern volatile int16_t jag_phase_pending;    // X axis
extern volatile int16_t jag_phase_y_pending;  // Y axis — mouse mode only

// Step interval in microseconds — derived from spinner_divisor by Core 0
extern volatile uint32_t jag_step_interval_us;

// Bitmask of GPIO pins owned by the phase driver (excludes from strobe response)
extern volatile uint32_t jag_phase_pin_mask;

// Current input mode
extern volatile jag_input_mode_t jag_input_mode;

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

extern const OutputInterface jaguar_output_interface;

// ============================================================================
// PUBLIC API
// ============================================================================

jag_input_mode_t jaguar_get_mode(void);
uint8_t          jaguar_get_spinner_divisor(void);
