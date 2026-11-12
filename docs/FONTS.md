# Fonts

The LCD and the web app use different fonts: the LCD's are below, the web app's are under
[Web app font](#web-app-font).

## LCD fonts

The LCD uses two typefaces:

- **Bitstream Charter** (serif) for all text on the Overview and Trends screens: titles, probe names,
  temperature values (bold), the second unit, status words, the time, the axis labels and the footer,
  at 12, 14 and 24 px. The 12 px sizes are used on the compact 1.8-inch screen.
- **Montserrat** (sans-serif, built into LVGL) for the setup screens (where codes are typed by hand)
  and for the Wi-Fi icon, which is a Montserrat symbol.

Charter files are in `firmware/components/display_service/fonts/` (about 26 KB of bitmap data in
total). They contain only basic Latin (U+0020 to U+007E) and the degree sign; there is no kerning
and no fallback, so a probe name with other characters shows blanks for them.

### Regenerating

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

### Licence

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

## Web app font

The web app uses **Roboto Flex**, Material 3's typeface, built into the firmware image and served by
the device, so it works with no internet (spec §18.2, decision D9).

- File: `web/src/assets/fonts/roboto-flex-latin.woff2` (67,004 bytes; SHA-256
  `49bf57712b4c84c5e1aa3ba7b3a55d2cf6a19fdc54a383163670bd363d995d8c`). The build gives it a hashed
  name under `/assets/`; the device serves it as `font/woff2` with a one-year immutable cache.
- Source: `ofl/robotoflex/RobotoFlex[GRAD,XOPQ,XTRA,YOPQ,YTAS,YTDE,YTFI,YTLC,YTUC,opsz,slnt,wdth,wght].ttf`
  from [google/fonts](https://github.com/google/fonts) at commit
  `8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5` (SHA-256
  `9b523f7d82593df0107173849ebb8c817471a1df4b4fb2c3cbf40cfd810c8281`, 1,787,292 bytes).
- What is kept: the weight axis (300 to 700) and the optical-size axis (8 to 144) stay variable;
  the other axes are fixed at their defaults. Basic Latin, Latin-1 (which has `°`, `±`, `·`, `×`),
  and `– — ‘ ’ “ ” • … ′ ″ − ← →`. Digits are tabular by default, so temperatures do not shift as
  they change. Hinting is dropped.
- Fallback: the system UI font, with `font-display: swap`, so text shows at once and switches when
  the font has loaded.

Regenerate with `fonttools` and `brotli` (not part of the normal build), for example in a virtual
environment with `pip install fonttools==4.60.1 brotli==1.1.0`:

```
curl -LO "https://raw.githubusercontent.com/google/fonts/8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5/ofl/robotoflex/RobotoFlex%5BGRAD,XOPQ,XTRA,YOPQ,YTAS,YTDE,YTFI,YTLC,YTUC,opsz,slnt,wdth,wght%5D.ttf"
python3 tools/fonts/make_web_font.py 'RobotoFlex[GRAD,XOPQ,XTRA,YOPQ,YTAS,YTDE,YTFI,YTLC,YTUC,opsz,slnt,wdth,wght].ttf' \
    web/src/assets/fonts/roboto-flex-latin.woff2
```

A web test fails if the file grows past 80 KB.

### Licence

Roboto Flex is © 2017 The Roboto Flex Project Authors and licensed under the SIL Open Font
License 1.1. The full licence ships with the web build at `/licenses/RobotoFlex-OFL.txt`
(source: `web/public/licenses/RobotoFlex-OFL.txt`, from the same google/fonts commit).
