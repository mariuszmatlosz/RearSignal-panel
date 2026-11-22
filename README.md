# RearSignal

Author: Mariusz Matłosz. Original project code is licensed under MIT. Dependencies and manufacturer materials retain their respective licenses.

An OpenNextion ONX2432G028 V1.3 (ESP32-S3R8) touchscreen remote controlling a LUXONIC 16×96 LED panel over iPixel Color BLE.

## Firmware 0.2

Portrait LVGL interface, 240×320: dark background, Roboto with Polish characters, and three tabs with four 6:1 previews each, without scrolling. The top toolbar contains settings, an always-accessible OFF button, and a Bluetooth icon. Bluetooth is gray with a spinner while disconnected and blue when connected.

Tapping an item transfers a 96×16 GIF to the panel. The local animated preview, spinner, and progress bar start after the panel acknowledges reception and ON is sent. The remote sends OFF when the configured duration expires. A red outline indicates a transfer error. The 12 included animations are temporary demos to be replaced with the intended content.

Settings: turn the panel off after the first connection (enabled by default), duration of 1–60 seconds (default: 8 seconds, entered using a numeric keyboard), LCD PWM brightness of 5–100%, and separate panel brightness of 1–100%. Settings are stored in NVS. Brightness is saved when the slider is released; duration is saved when confirmed on the keyboard. The Info screen identifies RearSignal, Mariusz Matłosz, and MIT. No clock is used.

The remote searches for `LED_BLE_C474453D` and uses service `000000fa-0000-1000-8000-00805f9b34fb`, FA02 writes, and FA03 notifications. GIF transfers use CRC32 and acknowledged application windows of up to 12288 bytes, as in pypixelcolor; transport chunks are sized according to the MTU. OFF and a new selection finish the current protocol frame before processing the next command. The queue keeps the latest request. After an error, the connection is re-established and OFF is sent. Reconnection always starts with OFF, without automatically resuming an animation.

GATT ON/OFF writes do not read the panel's physical state. Automatic shutoff requires the remote to remain powered and connected over BLE. The panel's factory startup animation may appear before BLE connects. Disabling startup OFF leaves the panel's state unchanged on the first connection; it does not automatically send ON or start an animation.

## Building

Arduino ESP32 core 3.3.2. Libraries verified during project setup:

- GFX Library for Arduino 1.6.9
- Adafruit CST8XX Library 1.1.1
- Adafruit PCF8574 1.1.2
- Adafruit BusIO 1.17.4
- LVGL 8.3.11
- NimBLE-Arduino 2.3.6

In Arduino IDE, select ESP32S3 Dev Module, 16 MB flash, OPI PSRAM, and the 16M Flash (3MB APP/9.9MB FATFS) partition scheme. USB_UART uses a USB-to-UART converter; firmware logs are available on UART0 at 115200 baud. Leave USB CDC On Boot disabled for these logs.

```sh
arduino-cli compile --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=default' firmware/remote
arduino-cli upload --port /dev/cu.YOUR_PORT --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=default' firmware/remote
```

## Hardware testing

1. Disconnect iPixel and power the panel. Start the remote and verify BLE connection and panel shutoff.
2. Check all three tabs, each with four items and no scrolling.
3. Start a GIF. Verify panel animation, preview, and automatic OFF.
4. Press OFF during playback and transfer. Verify that the animation does not restart.
5. In settings, test LCD and panel brightness separately, durations of 1 and 60 seconds, and rejection of empty input, 0, and values above 60.
6. Restart and verify saved settings and the startup OFF option. Reconnection must not automatically resume playback.
7. Check the Info screen and OFF availability in both settings and Info.

UART diagnostics: `0` sends OFF, `1` turns on the stored content, `a`–`l` select the twelve GIFs, and `+`/`-` adjust panel brightness by 10 percentage points. Diagnostic brightness changes are not saved in NVS. Software tests do not replace visual inspection of the LCD and panel response.

## References

Pin assignments and LCD reset configuration come from the [OpenNextion documentation](https://github.com/OpenNextion/OpenNextion-SKU-ONX2432G028), reference revision `2aafcc6bf76d95abaccf53443b7cfa6bc257d452`. The firmware is a separate program built with standard libraries.

ON/OFF frames are described in the [iPixel protocol documentation](https://github.com/sphings79/ha-ble-led-pixel-display/blob/main/docs/protocol.md). Their operation was verified using Python/Bleak and visually on the user's panel on 2026-11-17. Firmware compilation and upload succeeded on 2026-11-17. The initial program uses 862415 bytes (27% of the partition), with 98616 bytes of static data (30% of RAM). esptool verified the uploaded program hash. Hardware logs confirmed touch initialization, panel discovery, BLE connection, OFF after connection, and ON/OFF writes requested over the serial port. The user confirmed correct operation of the touchscreen ON/OFF buttons and panel response on 2026-11-17.

The OpenNextion port is `/dev/cu.wchusbserial210` (WCH VID:PID 1A86:7522). The signed CH34xVCPDriver supplied in the official OpenNextion repository was activated. Port `usbmodem210MARZMZ3502` belongs to the LG monitor.

Local binaries and the test log are in the ignored `build/power-mvp/` directory. The full factory flash backup (16 MB) is stored at `build/backups/onx2432g028-factory-2026-11-17.bin`. It was read in chunks with retries for USB errors. Backup SHA-256: `ef94ed1b35dacb5a393fe1837e61b3a4082f2a1b3051c21aeae3a3fd20da22fc`.

## Enclosure

The manufacturer's PDF drawing, STEP model, and DWG file are in `hardware/enclosure/reference/`. Module dimensions from the drawing are 56 × 78.60 × 15.60 mm. Details and tolerances are documented in that directory's README.

## Firmware 0.2 validation (2026-11-17)

Build: 1078799 bytes of program storage (34%) and 148808 bytes of static data (45%). esptool verified the flash write. Startup: touch initialized successfully, UI created, and 64492 bytes of LVGL memory available. BLE tests confirmed reception of 5099-byte and 3006-byte GIFs, ON, brightness adjustment from 28→38→28, transfer interruption using OFF, and timeout activation. The final OFF command was written successfully. Brightness previously set in the UI survived a restart. All 12 GIF files were checked: 96×16 pixels, four frames, and 180 ms per frame. Full visual inspection of the LCD and all settings remained pending user confirmation at this validation stage.

On this panel, 4 KB framing caused a CRC mismatch for a larger GIF. Using 12 KB windows, as in [pypixelcolor 0.5.0](https://github.com/lucagoc/pypixelcolor), successfully transferred the 5099-byte file. Files larger than 12 KB had not yet been tested. Binaries, compilation and upload logs, and the hardware test log are in `build/ui-v02/`. Application SHA-256: `2b9c74845b69b06d4cef3af143d13b6bf3d69bdbbcf8e0ac15dc67c937210af6`.
