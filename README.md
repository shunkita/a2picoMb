# a2picoMb

Single-slot Mockingboard sound card emulator for the Apple II based on the Raspberry Pi RP2350 (`a2pico2`).

## Features

- Single Mockingboard card emulation: 2x 6522 VIA (`$Cn00-$Cn0F`, `$Cn80-$Cn8F`) and 2x AY-3-8913 PSG (`ayumi`, VIA #0 = Left, VIA #1 = Right)
- SSI-263A speech synthesis (`$Cn40-$Cn7F`, VIA #1 CA1 handshake)
- Two audio output variants: Bluetooth A2DP Source (`BT`) and USB Audio Class 2.0 (`USB-DEV`)

## Firmware Variants

| Variant | `.uf2` File | Supported Hardware | Audio Output | LED |
| --- | --- | --- | --- | --- |
| **BT** | `a2picoMb-BT_<VERSION>.uf2` | Pico 2 W / A2Pico2Lite W or A2Pico2 WE (CYW43 radio required) | Bluetooth A2DP (44.1 kHz stereo SBC) | ON when connected, OFF when disconnected |
| **USB-DEV** | `a2picoMb-USB-DEV_<VERSION>.uf2` | Pico 2 / A2Pico2Lite or A2Pico2 (or Pico 2 W / A2Pico2Lite W or A2Pico2 WE) | USB Audio Class 2.0 stereo input (`48 kHz`, 16-bit) | Not used |

## How to Use

### Bluetooth A2DP (`a2picoMb-BT_<VERSION>.uf2`)

1. Flash `a2picoMb-BT_<VERSION>.uf2` to an A2Pico2Lite W or A2Pico2 WE and put your Bluetooth speaker in pairing mode.
2. Insert the card into an Apple II expansion slot and power on.
3. Wait for the onboard LED to turn on (A2DP connected), then launch Mockingboard software.

### USB Audio Device (`a2picoMb-USB-DEV_<VERSION>.uf2`)

1. Flash `a2picoMb-USB-DEV_<VERSION>.uf2` to an A2Pico2Lite, A2Pico2Lite W, A2Pico2 or A2Pico2 WE.
2. Insert the card into an Apple II expansion slot, connect the Pico USB port to your host PC, and power on.
3. Select `A2PicoMb USB Audio (<id>)` (`48 kHz`, 16-bit stereo) as the host audio input.
4. Launch Mockingboard software on the Apple II.
