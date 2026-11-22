# Graphics assets

Demo-01…12 are original temporary RearSignal animations (MIT), 96×16 pixels, with four looping frames of 180 ms each. They are not the final message set. The `tools/generate_assets.py` script generates GIFs and matching RGB565 previews from a single source.

Roboto: [Google Fonts source](https://github.com/google/fonts/tree/main/ofl/roboto), licensed under the SIL Open Font License 1.1; see `Roboto-OFL.txt`. `roboto16.c` was generated using lv_font_conv 1.5.3: 16 px, 4 bpp, U+0020–007F, U+00A0–00FF, and Polish characters. Roboto is the UI font; toolbar symbols use LVGL's default font.

Ford logo: [Wikimedia Commons reference](https://commons.wikimedia.org/wiki/File:Ford_logo_flat.svg), with the [original SVG](https://upload.wikimedia.org/wikipedia/commons/3/3e/Ford_logo_flat.svg). The Ford logo and trademark retain their owner's rights and are not covered by the project's MIT license. `ford.png` is a 100×40 raster version for the LCD. Its use does not imply any affiliation between this project and Ford.

The `assets.h` header stores GIFs and previews in flash memory. Regenerate it after changing the graphics. No filesystem or SD card is required.
