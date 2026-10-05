# a2picoMb

Mockingboard sound card emulator for the Apple II based on the Raspberry Pi Pico 2 W (RP2350).
It emulates dual 6522 VIAs and dual AY-3-8910 sound chips, wirelessly streaming synthesized audio to a Bluetooth speaker via A2DP.

## Features

- Mockingboard emulation (2x 6522 VIA, 2x AY-3-8910 PSG)
- Wireless audio output via Bluetooth A2DP
- Onboard LED indicator for Bluetooth connection status

## How to Use

1. **Flash firmware**: Flash the compiled `.uf2` file onto the Raspberry Pi Pico 2 W.
2. **Prepare speaker**: Set your Bluetooth speaker to pairing mode.
3. **Install board**: Insert the a2picoMb card into an expansion slot of your Apple II.
4. **Power on**: Turn on the Apple II.
5. **Connection**: Once successfully connected to the Bluetooth speaker, the onboard LED turns on.
6. **Play**: Launch any Mockingboard-compatible software on the Apple II. Audio will play through the connected Bluetooth speaker.
