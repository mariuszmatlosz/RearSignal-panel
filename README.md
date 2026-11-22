# RearSignal

A touchscreen remote for compatible iPixel Color 96×16 LED panels. RearSignal runs on the OpenNextion ONX2432G028 V1.3 (ESP32-S3R8) and controls the panel over BLE.

Author: Mariusz Matłosz. Original project code is licensed under MIT. Dependencies and third-party assets retain their respective licenses.

## Features

- Portrait 240×320 LVGL interface with a dark background and Roboto font.
- Four native LVGL tabs with four graphical message buttons each, without vertical scrolling.
- Sixteen static or animated messages, transferred to the panel as GIFs.
- Image previews generated from the same artwork as the panel content. Animated button previews show four sampled frames rather than the complete GIF.
- An always-accessible OFF button, Bluetooth connection status, and playback indicators.
- BLE discovery, device selection, saved-device reconnection, and a Forget option.
- Configurable playback duration of 1–60 seconds. The three warning triangles remain on until OFF or another message is selected.
- Separate LCD and LED panel brightness controls, with settings saved in NVS.

## Connecting a panel

Power the remote and a compatible iPixel 96×16 panel. Disconnect other apps that may be using the panel, open the Bluetooth screen, scan, and select the device. The selection is saved for automatic reconnection. Use Forget to clear it and select another panel.

The firmware checks the panel's reported device type and password flag. Device selection is a BLE GATT connection, not authenticated Bluetooth bonding. Panels reporting that a password is required are not supported by the current connection flow. Saving a device does not prevent other controllers from accessing the panel.

## Playback and settings

Tap a graphical message button to transfer its GIF to the panel. After reception is acknowledged and ON is sent, the playback timer starts. The remote sends OFF when the configured duration expires, except for the unlimited warning-triangle message. OFF or another selection interrupts playback; an ongoing transfer finishes its current protocol frame before processing the new request.

Settings include startup OFF, playback duration (default: 8 seconds), LCD brightness of 5–100%, and panel brightness of 1–100%. Brightness is saved when the slider is released; duration is saved when confirmed using the numeric keyboard.

Startup OFF sends OFF after the first BLE connection. Disabling it leaves the panel's existing state unchanged on that connection. Reconnection always sends OFF and does not automatically resume a message.

Automatic shutoff requires the remote to remain powered and connected. ON/OFF writes do not provide a readback of the panel's physical state. A panel's factory startup animation may appear before BLE connects.

## Building and uploading

Use Arduino ESP32 core 3.3.2 with:

- GFX Library for Arduino 1.6.9
- Adafruit CST8XX Library 1.1.1
- Adafruit PCF8574 1.1.2
- Adafruit BusIO 1.17.4
- LVGL 8.3.11
- NimBLE-Arduino 2.3.6

In Arduino IDE, select ESP32S3 Dev Module, 16 MB flash, OPI PSRAM, and the 16M Flash (3MB APP/9.9MB FATFS) partition scheme. Leave USB CDC On Boot disabled.

USB_UART uses a CH34x USB-to-UART controller. Install a CH34x driver if required by your operating system.

Run these commands from the repository root, replacing `SERIAL_PORT` with the board's serial port:

```sh
arduino-cli compile --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=default' firmware/remote
arduino-cli upload --port SERIAL_PORT --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=default' firmware/remote
```

## Editing graphics

Message definitions are in `firmware/remote/assets/messages.json`. Icon drawing and animation logic are in `tools/generate_assets.py`.

After editing the artwork, regenerate the assets before compiling the firmware:

```sh
python3 -m pip install -r tools/requirements.txt
python3 tools/generate_assets.py
```

The generator writes GIFs, PNG previews, and `firmware/remote/assets.h`. Assets are embedded in flash; no filesystem or SD card is required. See the [asset README](firmware/remote/assets/README.md) for attribution.

## Functional checks

1. Scan for a panel, select it, and verify reconnection after a restart.
2. Check all four tabs and their graphical buttons.
3. Verify animated and static content, timed shutoff, and the unlimited warning triangles.
4. Test OFF during playback and transfer, and when selecting another message.
5. Check both brightness controls, duration limits, and settings persistence.
6. Verify startup OFF, reconnection behavior, and availability of OFF in settings and Info.
7. Use Forget and verify that another compatible panel can be selected.

UART diagnostics: `0` sends OFF, `1` turns on the panel's current content, `a`–`p` select the sixteen messages, and `+`/`-` adjust panel brightness by 10 percentage points without saving it. Line commands start with `:` and end with a newline: `:scan`, `:pair INDEX` (zero-based discovery index), `:forget`, `:status`, and `:time SECONDS`. The diagnostic duration override is not saved.

## Protocol and references

Communication uses service `000000fa-0000-1000-8000-00805f9b34fb`, FA02 writes, and FA03 notifications. GIF transfers use CRC32 and acknowledged application windows of up to 12288 bytes, split into GATT writes according to the negotiated MTU.

- [OpenNextion documentation and examples](https://github.com/OpenNextion/OpenNextion-SKU-ONX2432G028), reference revision `2aafcc6bf76d95abaccf53443b7cfa6bc257d452`, for hardware pin assignments and LCD reset configuration.
- [iPixel protocol documentation](https://github.com/sphings79/ha-ble-led-pixel-display/blob/main/docs/protocol.md).
- [pypixelcolor](https://github.com/lucagoc/pypixelcolor), for protocol reference and transfer framing.

## Enclosure

Manufacturer PDF, STEP, and DWG references are in `hardware/enclosure/reference/`. The module measures 56 × 78.60 × 15.60 mm according to the drawing. See the [mechanical reference README](hardware/enclosure/reference/README.md) for tolerances and enclosure clearances.
