# ============================================================================
# Retro Frog board-specific build settings
#
# Included from the end of CMakeLists.txt so these settings stay out of the
# shared file. Every target referenced here must already be defined.
# ============================================================================

# ----------------------------------------------------------------------------
# Longer XOSC startup delay for Retro Frog RP2354A PCBs.
#
# These boards use the YXC YSX321SL 12 MHz crystal (80 ohm max ESR, 20 pF load,
# 33 pF load caps). That is well outside Raspberry Pi's reference design
# (ABM8-272-T3: 50 ohm max ESR, 10 pF load), and roughly 2-3% of units fail to
# boot reliably from external power with the default startup delay.
#
# 64 gives about 64 ms of wait at 12 MHz, which is unnoticeable at boot.
# ----------------------------------------------------------------------------
set(_retrofrog_xosc_targets
    joypad_ami_retrofrog
    joypad_jag_rp2354a
    joypad_neogeo_retrofrog
)
foreach(_t IN LISTS _retrofrog_xosc_targets)
    if(TARGET ${_t})
        target_compile_definitions(${_t} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
    else()
        message(WARNING "retrofrog.cmake: target ${_t} not found; XOSC delay not applied")
    endif()
endforeach()
