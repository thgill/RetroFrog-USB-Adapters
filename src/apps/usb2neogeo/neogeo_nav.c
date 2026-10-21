// neogeo_nav.c - USB4NeoGeo Navigation & UX System
// Retro Frog USB4NeoGeo

#include "neogeo_nav.h"
#include "app.h"
#include "core/buttons.h"
#include "core/input_event.h"
#include "core/services/profiles/profile.h"
#include "core/services/profiles/profile_indicator.h"
#include "core/services/leds/leds.h"
#include "core/services/storage/flash.h"
#include "core/services/players/manager.h"
#include "platform/platform.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include "bootsel.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// LED COLORS
// ============================================================================

#define LED_OFF_R        0,   0,   0
#define LED_GREEN_R      0,  64,   0
#define LED_RED_R       64,   0,   0
#define LED_WHITE_R     32,  32,  32
#define LED_BLUE_R       0,   0,  64
#define LED_YELLOW_R    64,  40,   0
#define LED_PURPLE_R    32,   0,  64   // DPI adjust mode

// ============================================================================
// STATE
// ============================================================================

static nav_state_t      nav_state            = NAV_STATE_NORMAL;
static nav_color_mode_t color_mode           = NAV_MODE_PAD;
static uint8_t          pad_profile          = 0;
static bool             custom_active        = false;
static uint8_t          remap_saved[NAV_REMAP_SLOTS] = {0};
static uint8_t          remap_temp[NAV_REMAP_SLOTS]  = {0};
static uint8_t          remap_slot           = 0;

static uint32_t         select_held_start_ms = 0;
static bool             nav_triggered        = false;
static uint32_t         prev_buttons         = 0;

static const uint32_t TURBO_ELIGIBLE[] = {
    JP_BUTTON_B1, JP_BUTTON_B2, JP_BUTTON_B3, JP_BUTTON_B4,
    JP_BUTTON_L1, JP_BUTTON_R1, JP_BUTTON_L2, JP_BUTTON_R2,
};
#define TURBO_ELIGIBLE_COUNT (sizeof(TURBO_ELIGIBLE)/sizeof(TURBO_ELIGIBLE[0]))

static uint32_t         turbo_mask           = 0;
static uint32_t         turbo_last_blink_ms  = 0;
static bool             turbo_phase_on       = true;
static struct repeating_timer turbo_timer;
static bool             turbo_timer_active   = false;
static volatile uint32_t turbo_held_mask     = 0; // which turboed buttons are physically held

// Maps JP_BUTTON turbo input to the Neo Geo output GPIO pin (via Pad A mapping)
static uint8_t turbo_jp_to_gpio_pin(uint32_t jp_btn)
{
    switch (jp_btn) {
        case JP_BUTTON_B1: return P1_NEOGEO_B1_PIN; // Cross   → Neo Geo A
        case JP_BUTTON_B2: return P1_NEOGEO_B2_PIN; // Circle  → Neo Geo B
        case JP_BUTTON_B3: return P1_NEOGEO_B3_PIN; // Square  → Neo Geo C
        case JP_BUTTON_B4: return P1_NEOGEO_B4_PIN; // Triangle→ Neo Geo D
        case JP_BUTTON_R1: return P1_NEOGEO_B5_PIN; // R1      → Neo Geo P5
        case JP_BUTTON_L1: return P1_NEOGEO_B6_PIN; // L1      → Neo Geo P6
        default:           return 255;
    }
}

// Timer callback — toggles GPIO pins for physically held turboed buttons
static bool turbo_timer_callback(struct repeating_timer *t)
{
    (void)t;
    if (!turbo_timer_active) return false; // cancelled — stop firing
    turbo_phase_on = !turbo_phase_on;
    for (uint8_t i = 0; i < TURBO_ELIGIBLE_COUNT; i++) {
        uint32_t btn = TURBO_ELIGIBLE[i];
        if (!(turbo_mask & btn)) continue;
        if (!(turbo_held_mask & btn)) continue;
        uint8_t pin = turbo_jp_to_gpio_pin(btn);
        if (pin == 255) continue;
        if (turbo_phase_on) {
            gpio_put(pin, 0);
            gpio_set_dir(pin, GPIO_OUT);
        } else {
            gpio_set_dir(pin, GPIO_IN);
            gpio_pull_up(pin);
        }
    }
    return true;
}

static void turbo_start_timer(void)
{
    if (!turbo_timer_active) {
        turbo_phase_on = true;
        add_repeating_timer_ms(-(NAV_TURBO_PERIOD_MS / 2), turbo_timer_callback, NULL, &turbo_timer);
        turbo_timer_active = true;
    }
}

static void turbo_stop_timer(void)
{
    if (turbo_timer_active) {
        cancel_repeating_timer(&turbo_timer);
        turbo_timer_active = false;
        turbo_phase_on = true;
        // Restore all turboed pins to input mode after stopping timer
        // Do this after cancel to handle any final callback that may have fired
        for (uint8_t i = 0; i < TURBO_ELIGIBLE_COUNT; i++) {
            if (!(turbo_mask & TURBO_ELIGIBLE[i])) continue;
            uint8_t pin = turbo_jp_to_gpio_pin(TURBO_ELIGIBLE[i]);
            if (pin != 255) {
                gpio_set_dir(pin, GPIO_IN);
                gpio_pull_up(pin);
            }
        }
    }
}

static uint32_t         blink_remaining      = 0;
static uint32_t         blink_last_ms        = 0;
static bool             blink_on             = false;

static bool             controller_connected = false;

// ============================================================================
// MOUSE STATE
// ============================================================================

static bool             mouse_connected      = false;
static nav_mouse_mode_t mouse_mode           = NAV_MOUSE_JOYSTICK;
static uint8_t          mouse_dpi            = NAV_DPI_DEFAULT;
static bool             dpi_adjust_mode      = false;
static uint32_t         last_dpi_change_ms   = 0;
static uint8_t          spinner_counter      = 0;
static int32_t          spinner_accum        = 0;
static bool             start_deferred       = false; // deferred Start fire when A+B+C+Start blocked
static bool             bootsel_last         = false;
static uint32_t         bootsel_press_start_ms = 0;
static uint32_t         last_mouse_buttons   = 0; // track button state between events


#define BOOTSEL_DPI_HOLD_MS  2000  // Hold BOOTSEL 2s to enter/exit DPI mode

// ============================================================================
// FLASH STORAGE
// Stored in custom_profile slot 3, name[12]:
//   [0..1] = magic 0x4E46
//   [2]    = color_mode
//   [3]    = pad_profile
//   [4]    = custom_active
//   [5..10]= remap[0..5]
// ============================================================================

#define NAV_FLASH_SLOT  3
#define NAV_MAGIC_0     0x4E
#define NAV_MAGIC_1     0x46

static void nav_save_to_flash(void)
{
    flash_t* s = flash_get_settings();
    if (!s) return;
    custom_profile_t* slot = &s->profiles[NAV_FLASH_SLOT];
    slot->name[0] = NAV_MAGIC_0;
    slot->name[1] = NAV_MAGIC_1;
    slot->name[2] = (uint8_t)color_mode;
    slot->name[3] = pad_profile;
    slot->name[4] = custom_active ? 1 : 0;
    memcpy(&slot->name[5], remap_saved, NAV_REMAP_SLOTS);
    flash_save_now(s); // Use immediate write, not deferred
}

static void nav_load_from_flash(void)
{
    flash_t* s = flash_get_settings();
    if (!s) goto defaults;
    const custom_profile_t* slot = &s->profiles[NAV_FLASH_SLOT];
    if ((uint8_t)slot->name[0] != NAV_MAGIC_0 ||
        (uint8_t)slot->name[1] != NAV_MAGIC_1) goto defaults;
    color_mode    = (nav_color_mode_t)(uint8_t)slot->name[2];
    pad_profile   = (uint8_t)slot->name[3];
    custom_active = slot->name[4] != 0;
    memcpy(remap_saved, &slot->name[5], NAV_REMAP_SLOTS);
    if (color_mode > NAV_MODE_CUSTOM)   color_mode  = NAV_MODE_PAD;
    if (pad_profile >= NAV_PAD_COUNT)   pad_profile = 0;
    return;
defaults:
    color_mode = NAV_MODE_PAD; pad_profile = 0;
    custom_active = false;
    memset(remap_saved, 0xFF, sizeof(remap_saved));
}

// Mouse settings — stored in custom_profile slot 2
// name[0..1] = magic 0x4D53 ("MS")
// name[2]    = mouse_mode
// name[3]    = mouse_dpi
#define MOUSE_MAGIC_0   0x4D
#define MOUSE_MAGIC_1   0x53

static void mouse_save_to_flash(void)
{
    flash_t* s = flash_get_settings();
    if (!s) return;
    custom_profile_t* slot = &s->profiles[NAV_MOUSE_FLASH_SLOT];
    slot->name[0] = MOUSE_MAGIC_0;
    slot->name[1] = MOUSE_MAGIC_1;
    slot->name[2] = (uint8_t)mouse_mode;
    slot->name[3] = mouse_dpi;
    flash_save_now(s);
}

static void mouse_load_from_flash(void)
{
    flash_t* s = flash_get_settings();
    if (!s) goto defaults;
    const custom_profile_t* slot = &s->profiles[NAV_MOUSE_FLASH_SLOT];
    if ((uint8_t)slot->name[0] != MOUSE_MAGIC_0 ||
        (uint8_t)slot->name[1] != MOUSE_MAGIC_1) goto defaults;
    mouse_mode = (nav_mouse_mode_t)(uint8_t)slot->name[2];
    mouse_dpi  = (uint8_t)slot->name[3];
    if (mouse_mode > NAV_MOUSE_SPINNER) mouse_mode = NAV_MOUSE_JOYSTICK;
    if (mouse_dpi < NAV_DPI_MIN || mouse_dpi > NAV_DPI_MAX) mouse_dpi = NAV_DPI_DEFAULT;
    return;
defaults:
    mouse_mode = NAV_MOUSE_JOYSTICK;
    mouse_dpi  = NAV_DPI_DEFAULT;
}

// ============================================================================
// LED HELPERS
// ============================================================================

static void set_led_for_mode(nav_color_mode_t mode)
{
    if (dpi_adjust_mode) {
        leds_set_color(LED_PURPLE_R);
        return;
    }
    if (mouse_connected) {
        if (mouse_mode == NAV_MOUSE_JOYSTICK) {
            leds_set_color(LED_BLUE_R);
        } else {
            leds_set_color(LED_YELLOW_R);
        }
        return;
    }
    switch (mode) {
        case NAV_MODE_PAD:    leds_set_color(LED_GREEN_R); break;
        case NAV_MODE_CUSTOM: leds_set_color(LED_RED_R);   break;
    }
}

static void start_blink(uint8_t count)
{
    blink_remaining = count * 2;
    blink_last_ms   = platform_time_ms();
    blink_on        = false;
    leds_set_color(LED_OFF_R);
}

static void blink_task(void)
{
    if (blink_remaining == 0) return;
    uint32_t now = platform_time_ms();
    if ((now - blink_last_ms) < 80) return;
    blink_last_ms = now;
    blink_on = !blink_on;
    blink_remaining--;
    if (blink_on) {
        // In remap mode blink back to white, otherwise current mode color
        if (nav_state == NAV_STATE_REMAP) {
            leds_set_color(LED_WHITE_R);
        } else {
            set_led_for_mode(color_mode);
        }
    } else {
        leds_set_color(LED_OFF_R);
    }
    if (blink_remaining == 0) {
        if (nav_state == NAV_STATE_REMAP) {
            leds_set_color(LED_WHITE_R);
        } else {
            set_led_for_mode(color_mode);
        }
    }
}

// ============================================================================
// PROFILE HELPERS
// ============================================================================

static void apply_active_profile(void)
{
    if (color_mode == NAV_MODE_CUSTOM) {
        // Use Pad A as base profile — our remap outputs JP_BUTTON values
        // that Pad A correctly maps to Neo Geo outputs
        profile_set_active(OUTPUT_TARGET_GPIO, NAV_PAD_START); // Pad A = index 5
    } else {
        profile_set_active(OUTPUT_TARGET_GPIO, NAV_PAD_START + pad_profile);
    }
    // Cancel system blink AFTER setting profile
    profile_indicator_init();
    leds_cancel_indicator();
    set_led_for_mode(color_mode);
}

static void clear_turbo(void)
{
    if (turbo_timer_active) {
        turbo_stop_timer();
        // Restore all turboed pins to input pull-up (inactive HIGH)
        for (uint8_t i = 0; i < TURBO_ELIGIBLE_COUNT; i++) {
            if (!(turbo_mask & TURBO_ELIGIBLE[i])) continue;
            uint8_t pin = turbo_jp_to_gpio_pin(TURBO_ELIGIBLE[i]);
            if (pin != 255) {
                gpio_set_dir(pin, GPIO_IN);
                gpio_pull_up(pin);
            }
        }
    }
    turbo_mask = 0;
    turbo_last_blink_ms = 0;
    turbo_held_mask = 0;
}

// ============================================================================
// REMAP: apply saved remap to raw buttons
// remap_saved[slot] = index into REMAP_INPUTS[] — which input maps to that slot
// We produce a JP_BUTTON bitmask for the 6 Neo Geo outputs
// ============================================================================

static const uint32_t REMAP_INPUTS[] = {
    JP_BUTTON_B1, JP_BUTTON_B2, JP_BUTTON_B3, JP_BUTTON_B4,
    JP_BUTTON_L1, JP_BUTTON_R1, JP_BUTTON_L2, JP_BUTTON_R2,
};
#define REMAP_INPUT_COUNT (sizeof(REMAP_INPUTS)/sizeof(REMAP_INPUTS[0]))

// REMAP_OUTPUTS: the JP_BUTTON values that Pad A maps to each Neo Geo output slot.
// Pad A: Cross(B1)→A, Circle(B2)→B, Square(B3)→C, Triangle(B4)→D, R1→K2, L1→K3
// So we output these JP_BUTTON values and let Pad A profile translate them to GPIO.
static const uint32_t REMAP_OUTPUTS[] = {
    JP_BUTTON_B1,  // → Neo Geo A  (via Pad A: Cross → A)
    JP_BUTTON_B2,  // → Neo Geo B  (via Pad A: Circle → B)
    JP_BUTTON_B3,  // → Neo Geo C  (via Pad A: Square → C)
    JP_BUTTON_B4,  // → Neo Geo D  (via Pad A: Triangle → D)
    JP_BUTTON_R1,  // → Neo Geo P5 (via Pad A: R1 → K2)
    JP_BUTTON_L1,  // → Neo Geo P6 (via Pad A: L1 → K3)
};

static uint32_t apply_custom_remap(uint32_t raw_buttons)
{
    uint32_t output = 0;

    // Dpad, Select and Start always pass through regardless of remap
    output |= raw_buttons & (JP_BUTTON_DU | JP_BUTTON_DD |
                             JP_BUTTON_DL | JP_BUTTON_DR |
                             JP_BUTTON_S1 | JP_BUTTON_S2);

    // If no custom map saved, only dpad/start pass through
    if (!custom_active) return output;

    // Apply face button remap
    for (uint8_t slot = 0; slot < NAV_REMAP_SLOTS; slot++) {
        uint8_t input_idx = remap_saved[slot];
        if (input_idx == 0xFF) continue;           // unmapped slot
        if (input_idx >= REMAP_INPUT_COUNT) continue;
        uint32_t input_btn  = REMAP_INPUTS[input_idx];
        uint32_t output_btn = REMAP_OUTPUTS[slot];
        if (raw_buttons & input_btn) {
            output |= output_btn;
            printf("[nav] remap slot=%d idx=%d in=0x%lx out=0x%lx\n", slot, input_idx, input_btn, output_btn);
        }
    }
    return output;
}

// ============================================================================
// NAVIGATION INPUT HANDLER
// ============================================================================

static void handle_nav_input(uint32_t buttons, uint32_t prev)
{
    uint32_t jp = buttons & ~prev;

    // Up — next profile within mode
    if (jp & JP_BUTTON_DU) {
        clear_turbo();
        if (color_mode == NAV_MODE_PAD) {
            pad_profile = (pad_profile + 1) % NAV_PAD_COUNT;
            apply_active_profile();
            start_blink(pad_profile + 1);
        }
        return;
    }

    // Down — previous profile within mode
    if (jp & JP_BUTTON_DD) {
        clear_turbo();
        if (color_mode == NAV_MODE_PAD) {
            pad_profile = (pad_profile == 0) ? NAV_PAD_COUNT - 1 : pad_profile - 1;
            apply_active_profile();
            start_blink(pad_profile + 1);
        }
        return;
    }

    // Right or Left — toggle between PAD and CUSTOM
    if ((jp & JP_BUTTON_DR) || (jp & JP_BUTTON_DL)) {
        clear_turbo();
        color_mode = (color_mode == NAV_MODE_PAD) ? NAV_MODE_CUSTOM : NAV_MODE_PAD;
        apply_active_profile();
        set_led_for_mode(color_mode);
        return;
    }

    // Start — begin remap sequence (purple only)
    if ((jp & JP_BUTTON_S2) && color_mode == NAV_MODE_CUSTOM) {
        nav_state  = NAV_STATE_REMAP;
        remap_slot = 0;
        memset(remap_temp, 0xFF, sizeof(remap_temp)); // 0xFF = unmapped
        leds_set_color(LED_WHITE_R);
        printf("[nav] remap sequence started\n");
        return;
    }

    // Face buttons — toggle turbo
    for (uint8_t i = 0; i < TURBO_ELIGIBLE_COUNT; i++) {
        if (jp & TURBO_ELIGIBLE[i]) {
            turbo_mask ^= TURBO_ELIGIBLE[i];
            start_blink(2);
            printf("[nav] turbo 0x%08lx mask=0x%08lx\n", TURBO_ELIGIBLE[i], turbo_mask);
            if (turbo_mask != 0) {
                turbo_start_timer();
            } else {
                turbo_stop_timer();
            }
            return;
        }
    }
}

// ============================================================================
// REMAP SEQUENCE HANDLER
// ============================================================================

static void handle_remap_input(uint32_t buttons, uint32_t prev)
{
    uint32_t jp = buttons & ~prev;
    printf("[nav] remap_input: buttons=0x%08lx prev=0x%08lx jp=0x%08lx slot=%d\n", 
           buttons, prev, jp, remap_slot);
    for (uint8_t i = 0; i < REMAP_INPUT_COUNT; i++) {
        if (jp & REMAP_INPUTS[i]) {
            remap_temp[remap_slot++] = i;
            start_blink(1);
            printf("[nav] remap slot %d -> input %d\n", remap_slot - 1, i);
            if (remap_slot >= NAV_REMAP_SLOTS) {
                // All slots mapped — go back to nav mode, keep white LED until Select released
                memcpy(remap_saved, remap_temp, sizeof(remap_saved));
                custom_active = true;
                nav_state = NAV_STATE_NAV;
                printf("[nav] remap complete (all slots)\n");
            }
            return;
        }
    }
}

// ============================================================================
// SPINNER OUTPUT
// ============================================================================

// Output the 7-bit counter on the Neo Geo DB15 signal pins.
// Bit order: UP=b0, DOWN=b1, LEFT=b2, RIGHT=b3, A=b4, B=b5, C=b6
// Open-drain: bit=1 → drive LOW (OUTPUT), bit=0 → release HIGH (INPUT+pullup)
static void spinner_output(uint8_t counter)
{
    const uint8_t pins[7] = {
        P1_NEOGEO_DU_PIN,  // b0
        P1_NEOGEO_DD_PIN,  // b1
        P1_NEOGEO_DL_PIN,  // b2
        P1_NEOGEO_DR_PIN,  // b3
        P1_NEOGEO_B1_PIN,  // b4 (A)
        P1_NEOGEO_B2_PIN,  // b5 (B)
        P1_NEOGEO_B3_PIN,  // b6 (C)
    };
    for (uint8_t i = 0; i < 7; i++) {
        bool bit = (counter >> i) & 1;
        if (bit) {
            gpio_set_dir(pins[i], GPIO_OUT);
            gpio_put(pins[i], 0);  // drive LOW
        } else {
            gpio_set_dir(pins[i], GPIO_IN);
            gpio_pull_up(pins[i]); // release HIGH
        }
    }
}

static void spinner_restore_pins(void)
{
    const uint8_t pins[7] = {
        P1_NEOGEO_DU_PIN, P1_NEOGEO_DD_PIN, P1_NEOGEO_DL_PIN, P1_NEOGEO_DR_PIN,
        P1_NEOGEO_B1_PIN, P1_NEOGEO_B2_PIN, P1_NEOGEO_B3_PIN,
    };
    for (uint8_t i = 0; i < 7; i++) {
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
    }
}

// ============================================================================
// MOUSE HANDLER
// ============================================================================

void neogeo_nav_handle_mouse(const input_event_t* event)
{
    if (event->type != INPUT_TYPE_MOUSE) return;

    uint32_t now = platform_time_ms();
    uint32_t buttons = event->buttons;
    last_mouse_buttons = buttons;

    bool lmb = (buttons & JP_BUTTON_B1) != 0; // Left mouse
    bool rmb = (buttons & JP_BUTTON_B2) != 0; // Right mouse
    bool mmb = (buttons & JP_BUTTON_S2) != 0; // Middle mouse → Select/Coin

    // Apply DPI divisor to movement
    int8_t dx = event->delta_x / mouse_dpi;
    int8_t dy = event->delta_y / mouse_dpi;

    // In DPI adjust mode — adjust sensitivity with L/R buttons, block button output
    // but allow movement so user can feel DPI change in real time
    if (dpi_adjust_mode) {
        if ((now - last_dpi_change_ms) >= NAV_DPI_DEBOUNCE_MS) {
            if (lmb && mouse_dpi > NAV_DPI_MIN) {
                mouse_dpi--;
                last_dpi_change_ms = now;
                printf("[mouse] DPI decreased to %d\n", mouse_dpi);
            } else if (rmb && mouse_dpi < NAV_DPI_MAX) {
                mouse_dpi++;
                last_dpi_change_ms = now;
                printf("[mouse] DPI increased to %d\n", mouse_dpi);
            }
        }
        // Block button outputs during DPI mode but fall through for movement
        lmb = false;
        rmb = false;
        mmb = false;
    }

    if (mouse_mode == NAV_MOUSE_SPINNER) {
        // ---- Spinner mode ----
        // Accumulate X into 7-bit counter
        spinner_accum -= dx; // negated for correct direction
        while (spinner_accum >= 1) {
            spinner_counter = (spinner_counter + 1) & 0x7F;
            spinner_accum--;
        }
        while (spinner_accum <= -1) {
            spinner_counter = (spinner_counter - 1) & 0x7F;
            spinner_accum++;
        }
        spinner_output(spinner_counter);

        // D button → Left mouse
        if (lmb) { gpio_set_dir(P1_NEOGEO_B4_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_B4_PIN, 0); }
        else      { gpio_set_dir(P1_NEOGEO_B4_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_B4_PIN); }

        // Block Select+Start (UniBIOS menu) and A+B+C+Select/Start (also triggers UniBIOS)
        bool start_and_select = rmb && mmb;
        bool abc_all = (spinner_counter & 0x70) == 0x70; // bits 4,5,6 = A+B+C all set
        bool block_select = start_and_select || (abc_all && mmb);
        bool block_start  = start_and_select || (abc_all && rmb);

        // Handle deferred Start — if A+B+C+Start blocked, queue for firing when counter clears
        if (rmb && block_start && abc_all && !start_and_select) {
            start_deferred = true; // queue Start for when counter moves out of 0x70-0x7F
        }
        if (!rmb) {
            start_deferred = false; // Start released, cancel any pending deferred fire
        }

        // Fire deferred Start if counter has cleared A+B+C condition
        bool fire_deferred_start = start_deferred && !abc_all && !start_and_select;
        if (fire_deferred_start) {
            start_deferred = false;
        }

        if ((rmb && !block_start) || fire_deferred_start) {
            gpio_set_dir(P1_NEOGEO_S2_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_S2_PIN, 0);
        } else {
            gpio_set_dir(P1_NEOGEO_S2_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_S2_PIN);
        }

        if (mmb && !block_select) { gpio_set_dir(P1_NEOGEO_S1_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_S1_PIN, 0); }
        else                      { gpio_set_dir(P1_NEOGEO_S1_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_S1_PIN); }

    } else {
        // ---- Joystick mode ----
        bool go_right = dx > 0;
        bool go_left  = dx < 0;
        bool go_down  = dy > 0;
        bool go_up    = dy < 0;

        if (go_right) { gpio_set_dir(P1_NEOGEO_DR_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_DR_PIN, 0); }
        else          { gpio_set_dir(P1_NEOGEO_DR_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_DR_PIN); }

        if (go_left) { gpio_set_dir(P1_NEOGEO_DL_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_DL_PIN, 0); }
        else         { gpio_set_dir(P1_NEOGEO_DL_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_DL_PIN); }

        if (go_down) { gpio_set_dir(P1_NEOGEO_DD_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_DD_PIN, 0); }
        else         { gpio_set_dir(P1_NEOGEO_DD_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_DD_PIN); }

        if (go_up) { gpio_set_dir(P1_NEOGEO_DU_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_DU_PIN, 0); }
        else       { gpio_set_dir(P1_NEOGEO_DU_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_DU_PIN); }

        // A → Left mouse
        if (lmb) { gpio_set_dir(P1_NEOGEO_B1_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_B1_PIN, 0); }
        else     { gpio_set_dir(P1_NEOGEO_B1_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_B1_PIN); }

        // Block Select+Start simultaneously to prevent UniBIOS menu
        bool joy_start_and_select = rmb && mmb;

        // Start → Right mouse
        if (rmb && !joy_start_and_select) { gpio_set_dir(P1_NEOGEO_S2_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_S2_PIN, 0); }
        else                              { gpio_set_dir(P1_NEOGEO_S2_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_S2_PIN); }

        // Select → Middle mouse (same as left/right mouse, simple open-drain)
        if (mmb && !joy_start_and_select) { gpio_set_dir(P1_NEOGEO_S1_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_S1_PIN, 0); }
        else                              { gpio_set_dir(P1_NEOGEO_S1_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_S1_PIN); }
    }
}


void neogeo_nav_set_mouse_connected(bool connected)
{
    if (connected == mouse_connected) return;
    mouse_connected = connected;
    if (!connected) {
        // Mouse disconnected — restore all pins and clear state
        spinner_restore_pins();
        spinner_counter = 0;
        spinner_accum   = 0;
        dpi_adjust_mode = false;
    }
    set_led_for_mode(color_mode);
    printf("[nav] mouse %s\n", connected ? "connected" : "disconnected");
}

bool neogeo_nav_mouse_connected(void)
{
    return mouse_connected;
}

void neogeo_nav_init(void)
{
    nav_load_from_flash();
    mouse_load_from_flash();
    if (color_mode == NAV_MODE_CUSTOM) {
        profile_set_active(OUTPUT_TARGET_GPIO, NAV_PAD_START);
    } else {
        profile_set_active(OUTPUT_TARGET_GPIO, NAV_PAD_START + pad_profile);
    }
    profile_indicator_init();
    leds_cancel_indicator();
    set_led_for_mode(color_mode);
    printf("[nav] init color=%d pad=%d custom=%d mouse_mode=%d dpi=%d\n",
           color_mode, pad_profile, custom_active, mouse_mode, mouse_dpi);
}

void neogeo_nav_task(void)
{
    uint32_t now = platform_time_ms();

    // Detect controller connect/disconnect for LED management only
    bool connected = (playersCount > 0);
    if (connected != controller_connected) {
        controller_connected = connected;
        set_led_for_mode(color_mode);
    }

    // Detect mouse connect/disconnect
    // Mouse detected if any player has INPUT_TYPE_MOUSE
    bool mouse_now = false;
    for (int i = 0; i < playersCount && i < MAX_PLAYERS; i++) {
        // Check if any connected device is a mouse via router output type
        // For now detect by checking if we received any mouse events recently
        // (mouse_connected is set by neogeo_nav_handle_mouse)
    }
    // mouse_connected is managed by handle_mouse — just update LED on change

    // BOOTSEL button detection
    // Single press → toggle Joystick/Spinner mouse mode
    // Hold 2s → enter/exit DPI adjust mode (Purple LED)
    bool bootsel_now = read_bootsel_button();

    if (bootsel_now && !bootsel_last) {
        // Button just pressed — start timer
        bootsel_press_start_ms = now;
    } else if (!bootsel_now && bootsel_last) {
        // Button just released
        uint32_t held = now - bootsel_press_start_ms;
        if (dpi_adjust_mode) {
            // Any press exits DPI mode (single tap)
            dpi_adjust_mode = false;
            mouse_save_to_flash();
            set_led_for_mode(color_mode);
            printf("[nav] DPI adjust mode OFF\n");
        } else if (held >= BOOTSEL_DPI_HOLD_MS) {
            // Long press — enter DPI adjust mode
            if (mouse_connected) {
                dpi_adjust_mode = true;
                set_led_for_mode(color_mode);
                printf("[nav] DPI adjust mode ON\n");
            }
        } else {
            // Short press — toggle mouse mode
            if (mouse_connected) {
                mouse_mode = (mouse_mode == NAV_MOUSE_JOYSTICK) ? NAV_MOUSE_SPINNER : NAV_MOUSE_JOYSTICK;
                if (mouse_mode == NAV_MOUSE_SPINNER) {
                    spinner_counter = 0;
                    spinner_accum   = 0;
                } else {
                    spinner_restore_pins();
                }
                mouse_save_to_flash();
                set_led_for_mode(color_mode);
                printf("[nav] mouse mode toggled to %d\n", mouse_mode);
            }
        }
        bootsel_press_start_ms = 0;
    }
    bootsel_last = bootsel_now;

    // Select hold detection
    // After NAV_HOLD_MS, force the Select GPIO pin HIGH (inactive) so the console
    // never sees Select held regardless of whether the controller sends a release report.
    // The pin stays forced HIGH until we see a report without Select pressed.
    bool select_held = (prev_buttons & JP_BUTTON_S1) != 0;

    if (select_held) {
        if (select_held_start_ms == 0) select_held_start_ms = now;
        uint32_t held = now - select_held_start_ms;

        if (held >= NAV_SELECT_SUPPRESS_MS) {
            // Force Select GPIO HIGH — console sees Select as released
            gpio_set_dir(P1_NEOGEO_S1_PIN, GPIO_IN);
            gpio_pull_up(P1_NEOGEO_S1_PIN);
        }

        if (held >= NAV_HOLD_MS && !nav_triggered) {
            nav_triggered = true;
            nav_state     = NAV_STATE_NAV;
            set_led_for_mode(color_mode);
            printf("[nav] nav mode entered\n");
        }
    } else {
        // Select released — restore to input with pull-up (HIGH/inactive)
        // The tap callback will drive it correctly on the next HID report
        gpio_set_dir(P1_NEOGEO_S1_PIN, GPIO_IN);
        gpio_pull_up(P1_NEOGEO_S1_PIN);

        if (nav_triggered) {
            printf("[nav] Select released: nav_state=%d remap_slot=%d\n", nav_state, remap_slot);
            if ((nav_state == NAV_STATE_REMAP) && remap_slot > 0) {
                memcpy(remap_saved, remap_temp, sizeof(remap_saved));
                custom_active = true;
                printf("[nav] remap partial save: %d slots mapped\n", remap_slot);
            }
            nav_save_to_flash();
            nav_state     = NAV_STATE_NORMAL;
            nav_triggered = false;
            prev_buttons &= ~JP_BUTTON_S1;
            select_held_start_ms = 0;
            set_led_for_mode(color_mode);
            printf("[nav] nav mode exited, saved\n");
        }
        select_held_start_ms = 0;
        nav_triggered        = false;
    }

    blink_task();

    if (turbo_mask != 0 && blink_remaining == 0) {
        if ((now - turbo_last_blink_ms) >= NAV_TURBO_BLINK_MS) {
            turbo_last_blink_ms = now;
            start_blink(2);
        }
    }

    // Periodically re-apply mouse button state in case events were sparse
    if (mouse_connected && !dpi_adjust_mode) {
        static uint32_t last_mouse_reapply_ms = 0;
        if ((now - last_mouse_reapply_ms) >= 8) {
            last_mouse_reapply_ms = now;
            bool mmb = (last_mouse_buttons & JP_BUTTON_S2) != 0;
            bool rmb = (last_mouse_buttons & JP_BUTTON_B2) != 0;
            bool start_and_select = rmb && mmb; // block in both modes
            bool abc_all = (mouse_mode == NAV_MOUSE_SPINNER) && ((spinner_counter & 0x70) == 0x70);
            bool block_select = start_and_select || (abc_all && mmb);
            bool block_start  = start_and_select || (abc_all && rmb);
            if (mouse_mode == NAV_MOUSE_SPINNER) {
                bool lmb = (last_mouse_buttons & JP_BUTTON_B1) != 0;
                if (lmb) { gpio_set_dir(P1_NEOGEO_B4_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_B4_PIN, 0); }
                else      { gpio_set_dir(P1_NEOGEO_B4_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_B4_PIN); }
            } else {
                bool lmb = (last_mouse_buttons & JP_BUTTON_B1) != 0;
                if (lmb) { gpio_set_dir(P1_NEOGEO_B1_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_B1_PIN, 0); }
                else     { gpio_set_dir(P1_NEOGEO_B1_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_B1_PIN); }
            }
            // Start — blocked if Select also pressed, or A+B+C+Start in spinner mode
            // Deferred Start fires when counter clears A+B+C condition
            bool fire_deferred = start_deferred && !abc_all && !start_and_select;
            if (fire_deferred) start_deferred = false;
            if ((rmb && !block_start) || fire_deferred) {
                gpio_set_dir(P1_NEOGEO_S2_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_S2_PIN, 0);
            } else {
                gpio_set_dir(P1_NEOGEO_S2_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_S2_PIN);
            }
            // Select — blocked if Start also pressed, or A+B+C+Select in spinner mode
            if (mmb && !block_select) { gpio_set_dir(P1_NEOGEO_S1_PIN, GPIO_OUT); gpio_put(P1_NEOGEO_S1_PIN, 0); }
            else                      { gpio_set_dir(P1_NEOGEO_S1_PIN, GPIO_IN);  gpio_pull_up(P1_NEOGEO_S1_PIN); }
        }
    }
}

uint32_t neogeo_nav_filter_buttons(uint32_t raw_buttons)
{
    uint32_t old = prev_buttons;
    prev_buttons = raw_buttons;

    // If Select is physically released, immediately reset the hold timer.
    if (!(raw_buttons & JP_BUTTON_S1) && select_held_start_ms != 0 && !nav_triggered) {
        select_held_start_ms = 0;
    }

    // Suppress Select from Neo Geo output only after it's been held for
    // NAV_SELECT_SUPPRESS_MS. Taps and short holds pass through normally.
    uint32_t filtered = raw_buttons;
    if (select_held_start_ms != 0) {
        uint32_t held = platform_time_ms() - select_held_start_ms;
        if (held >= NAV_SELECT_SUPPRESS_MS) {
            filtered &= ~JP_BUTTON_S1;
        }
    }

    if (nav_state == NAV_STATE_NAV) {
        handle_nav_input(filtered & ~JP_BUTTON_S1, old & ~JP_BUTTON_S1);
        return 0; // suppress all output during nav mode
    }

    if (nav_state == NAV_STATE_REMAP) {
        handle_remap_input(filtered & ~JP_BUTTON_S1, old & ~JP_BUTTON_S1);
        return 0; // suppress all output during remap
    }

    // Normal mode
    uint32_t output;
    if (color_mode == NAV_MODE_CUSTOM) {
        output = apply_custom_remap(filtered);
    } else {
        output = filtered;
    }

    // Hardware timer owns turboed GPIO pins — remove them from software output entirely.
    // Update turbo_held_mask so timer knows which buttons are physically held.
    if (turbo_mask != 0) {
        for (uint8_t i = 0; i < TURBO_ELIGIBLE_COUNT; i++) {
            uint32_t btn = TURBO_ELIGIBLE[i];
            if (!(turbo_mask & btn)) continue;
            uint8_t pin = turbo_jp_to_gpio_pin(btn);
            if (pin == 255) continue;

            bool btn_held = (raw_buttons & btn) != 0;
            if (btn_held) {
                turbo_held_mask |= btn;
                // Button held — timer owns pin, remove from software output
                output &= ~btn;
            } else {
                turbo_held_mask &= ~btn;
                // Button released — restore to input with pull-up (open-drain HIGH)
                gpio_set_dir(pin, GPIO_IN);
                gpio_pull_up(pin);
            }
        }

        // Same for custom mode — remove turboed input buttons from output
        if (color_mode == NAV_MODE_CUSTOM) {
            for (uint8_t slot = 0; slot < NAV_REMAP_SLOTS; slot++) {
                uint8_t input_idx = remap_saved[slot];
                if (input_idx == 0xFF || input_idx >= REMAP_INPUT_COUNT) continue;
                uint32_t input_btn = REMAP_INPUTS[input_idx];
                if (!(turbo_mask & input_btn)) continue;
                uint32_t output_btn = REMAP_OUTPUTS[slot];
                output &= ~output_btn; // timer owns this output
            }
        }
    }

    return output;
}

bool neogeo_nav_is_suppressed(void)
{
    return (nav_state == NAV_STATE_NAV || nav_state == NAV_STATE_REMAP);
}

uint32_t neogeo_nav_get_turbo_mask(void)
{
    return turbo_mask;
}

// Returns a GPIO pin bitmask (1 << pin_number) for all turboed buttons
uint32_t neogeo_nav_get_turbo_pin_mask(void)
{
    if (turbo_mask == 0) return 0;
    uint32_t pin_mask = 0;
    for (uint8_t i = 0; i < TURBO_ELIGIBLE_COUNT; i++) {
        if (!(turbo_mask & TURBO_ELIGIBLE[i])) continue;
        uint8_t pin = turbo_jp_to_gpio_pin(TURBO_ELIGIBLE[i]);
        if (pin != 255) pin_mask |= (1u << pin);
    }
    return pin_mask;
}

// Maps JP_BUTTON values to gpio_device_port_t mask fields
// Returns the GPIO pin mask for a given JP_BUTTON

bool neogeo_nav_turbo_phase_on(void)
{
    return true; // Not used in simple turbo mode
}

uint32_t neogeo_nav_get_turbo_gpio_mask(const void* port_void, uint32_t last_gpio_buttons)
{
    (void)port_void;
    (void)last_gpio_buttons;
    return 0; // Not used in simple turbo mode
}
