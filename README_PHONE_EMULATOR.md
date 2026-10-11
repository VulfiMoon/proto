# Toaster Blaster: phone emulator build

This build is intended for testing without the physical MAX7219 chain and Bluetooth gamepad.

## What changed

- `VIRTUAL_MATRIX` replaces the physical MAX7219 driver with a framebuffer of 16 chained 8x8 modules (128x8).
- `/frame` exposes the exact 128 column bytes that the firmware would send to the matrix chain.
- The phone page renders that framebuffer at ~30 FPS.
- The on-screen D-pad, ABXY, shoulders, START/SELECT and joystick use the existing `Gamepad::inputHandler` and the existing `createButtonMapping()` path.
- Face quick-test buttons remain as shortcuts, but they are not required for normal emulator operation.
- The physical chain order is preserved:
  `EYE(2) -> MOUTH(4) -> NOSE(1) -> MOUTH_FRONT(2) -> NOSE(1) -> MOUTH(4) -> EYE(2)`

## Use

1. Open the project in PlatformIO.
2. Build and upload to ESP32.
3. Connect the phone to Wi-Fi `ToasterBlaster`, password `protogen1`.
4. Open the IP printed in Serial Monitor (normally `192.168.4.1`).
5. Use the virtual gamepad. The large canvas is the virtual MAX7219 output.

## Important

The build has not been compiled in this environment because PlatformIO is not installed here. Compile it in PlatformIO before flashing.

To return to physical MAX7219 output, remove/comment `#define VIRTUAL_MATRIX` in `src/config.h`.


## OLED emulator

The phone UI now includes a live 128x64 SSD1306 OLED emulator. It reads the framebuffer selected by `U8G2HUDWrapper::setRenderedBuffer()` through `/oled`, so menus and screens are rendered by the firmware itself rather than duplicated in JavaScript.

The virtual gamepad still enters through the existing `Gamepad::inputHandler`, so the OLED menu is controlled by the same controller/input path as a physical gamepad.
