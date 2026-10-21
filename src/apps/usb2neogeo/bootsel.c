// bootsel.c - BOOTSEL button reader for RP2350A
// Must be a separate compilation unit so __no_inline_not_in_flash_func
// works correctly without ARM/Thumb interworking issues.

#include "bootsel.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

bool __no_inline_not_in_flash_func(read_bootsel_button)(void) {
    const uint CS_PIN_INDEX = 1;
    uint32_t flags = save_and_disable_interrupts();
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    2u << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 10000; ++i);
#if PICO_RP2350
    bool state = !(sio_hw->gpio_hi_in & SIO_GPIO_HI_IN_QSPI_CSN_BITS);
#else
    bool state = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));
#endif
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    0u << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    restore_interrupts(flags);
    return state;
}
