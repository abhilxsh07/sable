#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = ["playwright", "pillow"]
# ///
# Sable, a native port of NightPDF
# Copyright (C) 2021  Advaith Madhukar
# Copyright (C) 2026  Abhilash Kar
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 2
# of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

# Renders a grid of color swatches with the exact css filter strings that
# updateCSS() builds, one row per preset plus a few custom slider combinations,
# and reads back what Chromium painted. The C++ filter code has to land within
# 2 per channel of these numbers (tests/filter_test.cpp).
# port of app/helpers/private.ts updateCSS() (L118-148)
# see: https://github.com/Lunarequest/NightPDF/blob/mistress/app/helpers/private.ts#L118-L148
#
# Usage, from rework/:
#   uv run tests/golden/make_fixtures.py
# The first run downloads Chromium (python -m playwright install chromium).
import io
import json
import math
import os
import subprocess
import sys

from PIL import Image
from playwright.sync_api import Error, sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
OUTPUT = os.path.join(HERE, "filter_expected.json")

# the order of the six slider values everywhere below (same as the order of
# setupSliders): darkness, grayscale, inversion, sepia, hue, brightness
KEYS = [
    "brightness",
    "grayness",
    "inversion",
    "sepia",
    "hue",
    "extraBrightness",
]

# port of app/helpers/sliders.ts handlePresetChange() (L1-57)
PRESETS = {
    "default": [7, 95, 95, 55, 180, 0],
    "sepia": [0, 0, 25, 100, 0, -30],
    "redeye": [8, 100, 92, 100, 295, -6],
    "original": [0, 0, 0, 0, 0, 0],
}

# the same four as customValues() in tests/filter_test.cpp. The second one
# pushes the last brightness() to 3, the third one takes it down to 0.
CUSTOM = {
    "custom-mid": [35, 40, 60, 30, 90, 10],
    "extreme-bright": [0, 0, 0, 0, 200, 200],
    "extreme-dark": [60, 0, 100, 0, 60, -100],
    "custom-hue-sepia": [20, 100, 50, 100, 330, 60],
}

COLORS = [
    # white, black, grays
    (255, 255, 255),
    (0, 0, 0),
    (32, 32, 32),
    (64, 64, 64),
    (96, 96, 96),
    (128, 128, 128),
    (160, 160, 160),
    (192, 192, 192),
    (224, 224, 224),
    # primaries and secondaries
    (255, 0, 0),
    (0, 255, 0),
    (0, 0, 255),
    (255, 255, 0),
    (0, 255, 255),
    (255, 0, 255),
    # a few mid tones
    (200, 80, 40),
    (40, 160, 90),
    (70, 90, 200),
    (240, 200, 120),
    (120, 40, 160),
    (30, 30, 60),
    (255, 128, 0),
    (10, 200, 200),
    (150, 150, 100),
]

CELL = 32
SWATCH = 24


def js_round(value: float) -> int:
    # Math.round: halves go up
    return math.floor(value + 0.5)


def js_num(value: float) -> str:
    # how a javascript number looks inside a template string: 1, not 1.0
    text = repr(float(value))
    return text[:-2] if text.endswith(".0") else text


def css_filter(values: list[int]) -> str:
    """The string updateCSS() builds (private.ts 134-146)."""
    brightness, grayness, inversion, sepia, hue, extra_brightness = values
    # noUiSlider's get() returns strings with two decimals by default ("7.00"),
    # which only shows in the hue, the others go through a division
    return (
        "filter: "
        f"brightness({js_num((100 - brightness) / 100)}) "
        f"grayscale({js_num(grayness / 100)}) "
        f"invert({js_num(inversion / 100)}) "
        f"sepia({js_num(sepia / 100)}) "
        f"hue-rotate({float(hue):.2f}deg) "
        f"brightness({js_num((js_round(extra_brightness) + 100.0) / 100)});"
    )


def build_page(rows: list[tuple[str, str]]) -> str:
    # absolute positions on whole pixels, so every swatch has a clean centre
    swatches = []
    for row, (_, css) in enumerate(rows):
        for col, (r, g, b) in enumerate(COLORS):
            x = col * CELL + (CELL - SWATCH) // 2
            y = row * CELL + (CELL - SWATCH) // 2
            swatches.append(
                f'<div style="left:{x}px;top:{y}px;'
                f'background:rgb({r},{g},{b});{css}"></div>'
            )
    return (
        "<!doctype html><html><head><meta charset='utf-8'><style>"
        "html,body{margin:0;padding:0;background:#fff}"
        f"div{{position:absolute;width:{SWATCH}px;height:{SWATCH}px}}"
        "</style></head><body>" + "".join(swatches) + "</body></html>"
    )


def launch(playwright):
    # --force-color-profile keeps Chromium from color managing the screenshot
    args = ["--force-color-profile=srgb"]
    try:
        return playwright.chromium.launch(headless=True, args=args)
    except Error as error:
        if "Executable doesn't exist" not in str(error):
            raise
    print("Chromium is missing, installing it")
    subprocess.run(
        [sys.executable, "-m", "playwright", "install", "chromium"], check=True
    )
    return playwright.chromium.launch(headless=True, args=args)


def render(rows: list[tuple[str, str]]) -> Image.Image:
    with sync_playwright() as playwright:
        browser = launch(playwright)
        try:
            page = browser.new_page(
                viewport={
                    "width": len(COLORS) * CELL,
                    "height": len(rows) * CELL,
                },
                device_scale_factor=1,
            )
            page.set_content(build_page(rows))
            png = page.screenshot(type="png")
        finally:
            browser.close()
    return Image.open(io.BytesIO(png)).convert("RGB")


def main() -> None:
    sets = {**PRESETS, **CUSTOM}
    # row 0 has no filter at all: if those pixels don't come back unchanged,
    # the screenshot isn't a clean copy of the colors and nothing below counts
    rows = [("control", "")] + [(n, css_filter(v)) for n, v in sets.items()]
    image = render(rows)

    for col, color in enumerate(COLORS):
        got = image.getpixel((col * CELL + CELL // 2, CELL // 2))
        if got != color:
            sys.exit(f"control swatch {color} came back as {got}, not usable")

    result = []
    for row, (name, values) in enumerate(sets.items(), start=1):
        cases = []
        for col, color in enumerate(COLORS):
            x = col * CELL + CELL // 2
            y = row * CELL + CELL // 2
            out = image.getpixel((x, y))
            cases.append({"in": list(color), "out": list(out)})
        result.append(
            {
                "name": name,
                "values": dict(zip(KEYS, values)),
                "css": rows[row][1],
                "cases": cases,
            }
        )

    # one entry per line keeps the diff readable
    with open(OUTPUT, "w", newline="\n") as file:
        file.write("[\n")
        file.write(",\n".join(json.dumps(entry) for entry in result))
        file.write("\n]\n")
    print(f"wrote {len(result)} filters x {len(COLORS)} colors to {OUTPUT}")


if __name__ == "__main__":
    main()
