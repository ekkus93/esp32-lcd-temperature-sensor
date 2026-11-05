# LCD fonts

The LCD uses two typefaces:

- **Bitstream Charter** (serif) for the screen title, probe names and the temperature values, in
  regular and bold at 12, 14 and 24 px. The 12 px sizes are used on the compact 1.8-inch screen.
- **Montserrat** (sans-serif, built into LVGL) for small status text, the time, the axis labels,
  the setup screens (where codes are typed by hand) and the Wi-Fi icon, which is a Montserrat symbol.

Charter files are in `firmware/components/display_service/fonts/` (about 26 KB of bitmap data in
total). They contain only basic Latin (U+0020 to U+007E) and the degree sign; there is no kerning
and no fallback, so a probe name with other characters shows blanks for them.

## Regenerating

The fonts are made offline from the Type 1 Charter files that ship with the `xfonts-scalable`
package. This needs `fonttools` and `Pillow` (not part of the normal build):

```
python3 tools/fonts/t1_to_ttf.py /usr/share/fonts/X11/Type1/c0648bt_.pfb charter-regular.ttf "Bitstream Charter" Regular
python3 tools/fonts/t1_to_ttf.py /usr/share/fonts/X11/Type1/c0632bt_.pfb charter-bold.ttf "Bitstream Charter" Bold
python3 tools/fonts/make_lvgl_font.py charter-regular.ttf 14 font_charter_regular_14 \
    firmware/components/display_service/fonts/font_charter_regular_14.c --notice NOTICE.txt
```

(`NOTICE.txt` is the licence text below.) `make_lvgl_font.py` is a small stand-in for `lv_font_conv`
using Pillow's FreeType rasterizer.

## Licence

Bitstream Charter is used under this notice, which must stay with copies of the font (it is also
in the header of every generated font file):

> (c) Copyright 1989-1992, Bitstream Inc., Cambridge, MA.
>
> You are hereby granted permission under all Bitstream propriety rights to use, copy, modify,
> sublicense, sell, and redistribute the 4 Bitstream Charter (r) Type 1 outline fonts and the 4
> Courier Type 1 outline fonts for any purpose and without restriction; provided, that this notice
> is left intact on all copies of such fonts and that Bitstream's trademark is acknowledged as
> shown below on all unmodified copies of the 4 Charter Type 1 fonts.
>
> BITSTREAM CHARTER is a registered trademark of Bitstream Inc.
