<p align="center">
  <img src="docs/images/retro_frog_logo.png" alt="Retro Frog" width="300">
</p>

<p align="center">
  <strong>Firmware for Retro Frog USB controller adapters</strong>
</p>

<p align="center">
  Use modern USB HID controllers and mice on classic computers — no drivers, no configuration, no compromises.
</p>

<p align="center">
  <a href="https://retrofrog.net"><img src="https://img.shields.io/badge/Website-retrofrog.net-ff69b4?style=for-the-badge" alt="Website" /></a>
  <a href="https://retrofrog.net/"><img src="https://img.shields.io/badge/Store-retrofrog.net-ff69b4?style=for-the-badge" alt="Store" /></a>
  <a href="https://bsky.app/profile/retrofrog.bsky.social"><img src="https://img.shields.io/badge/Bluesky-retrofrog-0285FF?style=for-the-badge&logo=bluesky" alt="Bluesky" /></a>
  <a href="https://x.com/ToddsNerdCave"><img src="https://img.shields.io/badge/X-ToddsNerdCave-000000?style=for-the-badge&logo=x" alt="X" /></a>
  <a href="https://github.com/thgill/joypad-os/releases"><img src="https://img.shields.io/github/downloads/thgill/joypad-os/total?style=for-the-badge&label=Downloads" alt="Downloads" /></a>
  <a href="https://github.com/thgill/joypad-os/blob/main/LICENSE"><img src="https://img.shields.io/github/license/thgill/joypad-os?style=for-the-badge" alt="License" /></a>
</p>

---

## Products

### USB4AMI — USB HID to Amiga / C64 / Atari


Use any modern USB gamepad or mouse with your Commodore Amiga, Commodore 64, Atari ST, and more. Plugs directly into the DE9 joystick/mouse port with no modification to your computer required.

**Supported computers:**

| Mode | Computers |
|------|-----------|
| **Amiga** | Commodore Amiga (all models) |
| **C64** | Commodore 64 (including Ultimate64), Commodore 128, MEGA65 |
| **Atari** | Atari ST (all), Atari Falcon, Atari 8-bit computers (all) |

> **Note:** The ZX Spectrum with Kempston joystick interface is also supported. You can use any of the 3 modes for it as the ZX supports just a single joystick button.

**Supported input:**
- USB gamepads — Xbox, PlayStation, Nintendo Switch, 8BitDo, and most generic HID controllers
- USB mice — any standard USB mouse or trackball

**Output protocols:**
- Amiga joystick and quadrature mouse
- CD32 controller (auto-detected on Amiga)
- Commodore 64 joystick and C1351 proportional mouse
- Atari joystick and quadrature mouse

**[USB4AMI User Guide →](docs/retrofrog/usb4ami/user_guide.md)**

**[Firmware Releases →](https://github.com/thgill/joypad-os/releases)**

---

### USB4NEO — USB HID to Neo Geo and Supergun

Use any modern USB HID gamepad or joystick with your Neo Geo AES, MVS, CD consoles and most Supergun setups that have the standard Neo Geo DB15 controller connector.

**Supported input:**
- USB gamepads — Xbox, PlayStation, Nintendo Switch, 8BitDo, and most generic HID controllers
- USB mice — any standard USB mouse or trackball

**Output protocols:**
- Neo Geo 4 button controller
- Supergun 6 button controller
- Neo Geo Paddle support for supported games via Mouse or Trackball
- Neo Geo Mouse as Joystick for games like Puzzle Bobble

**[USB4NEO User Guide →](docs/retrofrog/usb4neo/user_guide.md)**

**[Firmware Releases →](https://github.com/thgill/joypad-os/releases)**

---

## Flashing Firmware

1. Download the latest `.uf2` from [Releases](https://github.com/thgill/RetroFrog-USB-Adapters/releases)
2. **Disconnect the adapter from your retro computer/console first**
3. Hold the BOOTSEL button and connect the USB-A cable to your computer
4. Drag the `.uf2` file onto the `RP2350` drive that appears
5. Done — the drive ejects and your adapter is running the new firmware

> **Note:** You MUST use a USB-A male to male cable to plug into the Retro Frog USB Adapter for firmware updates. A USB-A to USB-C will NOT work. This isn't because of any fault of the adapter design, it's due to the fact that USB-A to C cables lack the 5.1k CC resistors on the USB-C side that would tell your computer that it's attached to a device and need to send 5 volts to it. If you need to update the firmware and all you have on your modern computer is USB-C ports (hello Apple!), you can use a female USB-A to male USB-C adapter as they have the correct resistors and are designed for this exact purpose. 

---

## Building from Source

Retro Frog firmware is built on the [Joypad OS](https://github.com/joypad-ai/joypad-os) open-source firmware platform by Robert Dale Smith.

### Prerequisites

```bash
# macOS
brew install --cask gcc-arm-embedded
brew install cmake git
```

### Build

```bash
git clone https://github.com/thgill/joypad-os.git
cd joypad-os
make init

# USB4AMI (Retro Frog RP2354A board)
make usb2ami_retrofrog
```

Output: `releases/joypad_<commit>_usb2ami_retrofrog.uf2`

---

## Support

- **Website/Shop:** [retrofrog.net](https://retrofrog.net)
- **Discord:** [Discord](https://discord.gg/h2Nqva37rq)
- **Bluesky:** [@retrofrog.bsky.social](https://bsky.app/profile/retrofrog.bsky.social)
- **X:** [@ToddsNerdCave](https://x.com/ToddsNerdCave)
- **Issues:** [GitHub Issues](https://github.com/thgill/joypad-os/issues)

---

## License & Attribution

Retro Frog firmware is built on [Joypad OS](https://github.com/joypad-ai/joypad-os), created by [Robert Dale Smith](https://github.com/joypad-ai).

Both are licensed under the **[Apache License 2.0](LICENSE)**.

Retro Frog product additions and modifications are copyright © 2026 Todd Gill/Retro Frog.
