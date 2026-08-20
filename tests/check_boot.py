#!/usr/bin/env python3
"""Check what ME OS actually drew, without a human looking at the screen.

Reads the framebuffers QEMU captured and the kernel log, then asserts both
milestones:

  M1  black background, white text, the boot message on one centred line
  M2  a second line that says PRESS A KEY before any input, and reports the
      injected key afterwards

This is not a substitute for a person seeing it once on real hardware. It is
what keeps the milestones from silently breaking afterwards.

Run through `make test`.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCREEN_BOOT = ROOT / "build" / "screen-boot.ppm"
SCREEN_KEY = ROOT / "build" / "screen-key.ppm"
DEBUG_LOG = ROOT / "build" / "debug.log"
SERIAL_LOG = ROOT / "build" / "serial.log"

# These mirror kernel/src/main.c. If the kernel's wording changes, change it here.
M1_MESSAGE = "IF YOU SEE THIS IT WORKED"
M2_PROMPT = "PRESS A KEY"
KEY_SENT = "A"
M2_AFTER_KEY = f"LAST KEY {KEY_SENT}"

FONT_WIDTH = 8
FONT_HEIGHT = 8
# The 8x8 cell holds a 5x7 glyph: the last row is always blank so lines have a
# gap. Lit rows therefore measure seven cell rows, not the full eight.
INK_HEIGHT = 7


class CheckFailed(Exception):
    pass


class Band:
    """One horizontal line of white pixels."""

    def __init__(self, rows: set[int], columns: set[int]) -> None:
        self.top = min(rows)
        self.bottom = max(rows)
        self.height = self.bottom - self.top + 1
        self.columns = columns
        self.left = min(columns)
        self.right = max(columns)

    @property
    def centre_x(self) -> float:
        return (self.left + self.right) / 2

    @property
    def centre_y(self) -> float:
        return (self.top + self.bottom) / 2

    @property
    def glyphs(self) -> int:
        """Runs of lit columns, one per drawn character."""
        runs, previous = 0, None
        for column in sorted(self.columns):
            if previous is None or column != previous + 1:
                runs += 1
            previous = column
        return runs


def glyph_count(text: str) -> int:
    return len(text.replace(" ", ""))


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    """Parse the binary P6 file QEMU's screendump writes."""
    if not path.exists() or path.stat().st_size == 0:
        raise CheckFailed(f"no framebuffer capture at {path}, run make test")

    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise CheckFailed(f"{path} is not a binary PPM")

    fields, i = [], 2
    while len(fields) < 3:
        while i < len(data) and data[i : i + 1].isspace():
            i += 1
        if data[i : i + 1] == b"#":
            while data[i : i + 1] != b"\n":
                i += 1
            continue
        start = i
        while not data[i : i + 1].isspace():
            i += 1
        fields.append(int(data[start:i]))
    i += 1

    width, height, maxval = fields
    if maxval != 255:
        raise CheckFailed(f"unexpected maximum colour value {maxval}")
    pixels = data[i : i + width * height * 3]
    if len(pixels) != width * height * 3:
        raise CheckFailed("screenshot is truncated")
    return width, height, pixels


def find_bands(path: Path) -> tuple[int, int, list[Band]]:
    """Split the lit pixels into horizontal bands of text."""
    width, height, pixels = read_ppm(path)
    rows: dict[int, set[int]] = {}

    for index in range(0, len(pixels), 3):
        r, g, b = pixels[index], pixels[index + 1], pixels[index + 2]
        if r == 0 and g == 0 and b == 0:
            continue
        if not (r == g == b == 255):
            pixel = index // 3
            raise CheckFailed(
                f"{path.name}: pixel at ({pixel % width}, {pixel // width}) is "
                f"rgb({r},{g},{b}); only black and white are expected"
            )
        pixel = index // 3
        rows.setdefault(pixel // width, set()).add(pixel % width)

    if not rows:
        raise CheckFailed(f"{path.name}: the screen is entirely black")

    bands: list[Band] = []
    current_rows: set[int] = set()
    current_cols: set[int] = set()
    previous = None
    for row in sorted(rows):
        if previous is not None and row != previous + 1:
            bands.append(Band(current_rows, current_cols))
            current_rows, current_cols = set(), set()
        current_rows.add(row)
        current_cols |= rows[row]
        previous = row
    bands.append(Band(current_rows, current_cols))
    return width, height, bands


def check_line(band: Band, text: str, label: str, name: str) -> str:
    if band.height % INK_HEIGHT != 0:
        raise CheckFailed(
            f"{name}: the {label} band is {band.height} pixels tall, "
            f"not a whole multiple of the {INK_HEIGHT} pixel glyph height"
        )
    expected = glyph_count(text)
    if band.glyphs != expected:
        raise CheckFailed(
            f"{name}: the {label} line has {band.glyphs} glyphs, "
            f"expected {expected} for {text!r}"
        )
    return f"{label}: {band.glyphs} glyphs, scale {band.height // INK_HEIGHT}"


def check_screen(path: Path, key_line_text: str) -> list[str]:
    width, height, bands = find_bands(path)
    notes = [f"{path.name}: {width}x{height}, {len(bands)} text lines"]

    if len(bands) != 2:
        raise CheckFailed(
            f"{path.name}: expected two lines, the M1 message and the key line, "
            f"found {len(bands)}"
        )

    message, key_line = bands
    notes.append("  " + check_line(message, M1_MESSAGE, "M1 message", path.name))
    notes.append("  " + check_line(key_line, key_line_text, "M2 key line", path.name))

    scale = message.height // INK_HEIGHT
    if abs(message.centre_x - width / 2) > FONT_WIDTH * scale:
        raise CheckFailed(f"{path.name}: the M1 message is not horizontally centred")
    if abs(message.centre_y - height / 2) > FONT_HEIGHT * scale:
        raise CheckFailed(f"{path.name}: the M1 message is not vertically centred")
    if key_line.top <= message.bottom:
        raise CheckFailed(f"{path.name}: the key line is not below the M1 message")
    notes.append("  M1 message centred, key line below it")
    return notes


def check_log() -> list[str]:
    notes = []
    expected = (
        "kernel entered",
        "drew the M1 message",
        "keyboard ready",
        f"key {KEY_SENT}",
    )
    for path, name in ((DEBUG_LOG, "debug port"), (SERIAL_LOG, "serial port")):
        if not path.exists() or path.stat().st_size == 0:
            raise CheckFailed(f"no kernel output on the {name} ({path})")
        text = path.read_text(errors="ignore")
        if "FAILED" in text:
            failure = next(line for line in text.splitlines() if "FAILED" in line)
            raise CheckFailed(f"the kernel reported a failure: {failure.strip()}")
        for phrase in expected:
            if phrase not in text:
                raise CheckFailed(f"{name} log never reported {phrase!r}")
        notes.append(f"{name} log: boot, draw, keyboard ready, key {KEY_SENT} received")
    return notes


def main() -> int:
    try:
        notes = check_screen(SCREEN_BOOT, M2_PROMPT)
        notes += check_screen(SCREEN_KEY, M2_AFTER_KEY)
        notes += check_log()
    except CheckFailed as exc:
        print(f"check FAILED: {exc}")
        return 1

    for note in notes:
        print(f"  {note}")
    print("M1 and M2 checks passed: message drawn, key press seen and displayed")
    print("A person should still watch it boot once with make run.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
