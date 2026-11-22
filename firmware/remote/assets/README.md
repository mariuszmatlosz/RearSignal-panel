# Graphics assets

The sixteen message definitions are stored in `messages.json`. `tools/generate_assets.py` renders the original RearSignal artwork into 96×16 GIFs, PNG previews, and the RGB565 images embedded in `assets.h`.

GIF files are transferred to the LED panel. Button previews use four sampled frames per message at 192×32 pixels. Static messages repeat the same preview frame. The warning-triangle message is marked as unlimited in the message definitions.

Edit message text in `messages.json` and icon drawing or animation logic in the Python generator. Regenerate `assets.h` before rebuilding the firmware. Assets are stored in flash memory; no filesystem or SD card is required.

## Attribution

Original RearSignal artwork is covered by the project's MIT license. Third-party fonts and logos retain their own terms.

- Roboto: [Google Fonts source](https://github.com/google/fonts/tree/main/ofl/roboto), SIL Open Font License 1.1; see `Roboto-OFL.txt`. `roboto16.c` was generated using lv_font_conv 1.5.3: 16 px, 4 bpp, U+0020–007F, U+00A0–00FF, and Polish characters. Toolbar symbols use LVGL's default font.
- Ford: [logo reference](https://commons.wikimedia.org/wiki/File:Ford_logo_flat.svg) and [original SVG](https://upload.wikimedia.org/wikipedia/commons/3/3e/Ford_logo_flat.svg). `ford.png` is a 100×40 raster version for the LCD.
- Honda: [logo reference](https://commons.wikimedia.org/wiki/File:Honda.svg) and [original SVG](https://upload.wikimedia.org/wikipedia/commons/3/38/Honda.svg). `honda.png` is the raster source used by the generator.

Ford and Honda logos and trademarks are not covered by the project's MIT license. Their use does not imply affiliation with either manufacturer.
