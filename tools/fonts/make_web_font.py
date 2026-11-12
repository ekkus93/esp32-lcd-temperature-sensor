#!/usr/bin/env python3
"""Cut the web app's Roboto Flex down to what the pages need, as one WOFF2 file.

    python3 tools/fonts/make_web_font.py ROBOTOFLEX.ttf web/src/assets/fonts/roboto-flex-latin.woff2

The source is the variable font from google/fonts (see docs/FONTS.md for the pinned commit and
checksum). Only the weight and optical-size axes stay variable; the other axes are fixed at their
defaults, and the weight range is narrowed to what the Material 3 type scale and the temperature
read-outs use. Only Latin and a few typographic marks are kept. Needs `fonttools` and `brotli`
(not part of the normal build).
"""
import argparse
import sys
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

# Kept variable: weight (Material 3 uses 400 and 500; 300 and 700 for large read-outs and
# emphasis) and optical size (so 11 px labels and 57 px display text are each drawn for their size).
AXES = {'wght': (300, 700), 'opsz': (8, 144)}
# Basic Latin, Latin-1 (includes ° ± · × µ and the no-break space), and the marks the pages use:
# – — ‘ ’ “ ” • … ′ ″ − → ←.
UNICODES = [
    *range(0x20, 0x7F),
    *range(0xA0, 0x100),
    0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2032, 0x2033,
    0x2212, 0x2190, 0x2192,
]
# Roboto Flex's digits are tabular by default, so temperatures don't shift as they change; `pnum`
# gives proportional ones. `rvrn` swaps glyphs at some weights and must stay with the weight axis.
FEATURES = ['kern', 'liga', 'calt', 'ccmp', 'locl', 'mark', 'mkmk', 'pnum', 'rvrn']


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()

    font = TTFont(args.source)
    fixed = {axis.axisTag: AXES.get(axis.axisTag, axis.defaultValue) for axis in font['fvar'].axes}

    options = subset.Options()
    options.flavor = 'woff2'
    options.layout_features = FEATURES
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]  # keep copyright, names and the licence
    options.notdef_outline = True
    options.hinting = False  # browsers on the device's clients don't need TrueType hints
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=UNICODES)
    subsetter.subset(font)
    # Fix the other axes after subsetting: instancing the whole font first is slow, and fewer
    # glyphs leave fewer variations to resolve.
    font = instancer.instantiateVariableFont(font, fixed)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subset.save_font(font, str(args.output), options)
    print(f'{args.output}: {args.output.stat().st_size} bytes, axes {fixed}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
