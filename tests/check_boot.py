#!/usr/bin/env python3
"""Check what ME OS actually drew, without a human looking at the screen.

Reads the framebuffers QEMU captured and the kernel log, then asserts both
milestones:

  M1  black background, white text, the boot message on one centred line
  M2  a second line that says PRESS A KEY before any input, and reports the
      injected key afterwards
  M3  one solid rectangle, in its own colour, at the expected size and place
  M4  a cursor that starts where it should, moves when the mouse moves, keeps
      its shape, and stops at the edge of the screen instead of leaving it

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
SCREEN_MOUSE = ROOT / "build" / "screen-mouse.ppm"
SCREEN_CLAMP = ROOT / "build" / "screen-clamp.ppm"
DEBUG_LOG = ROOT / "build" / "debug.log"
SERIAL_LOG = ROOT / "build" / "serial.log"

# These mirror kernel/src/main.c. If the kernel's wording changes, change it here.
M1_MESSAGE = "IF YOU SEE THIS IT WORKED"
M2_PROMPT = "PRESS A KEY"
KEY_SENT = "A"
M2_AFTER_KEY = f"LAST KEY {KEY_SENT}"

# These mirror kernel/src/main.c too.
RECT_COLOUR = (60, 170, 220)
RECT_WIDTH_DIVISOR = 4
RECT_HEIGHT_DIVISOR = 14

CURSOR_COLOUR = (255, 214, 64)
CURSOR_START_X_DIVISOR = 4
CURSOR_START_Y_DIVISOR = 6
# Must match scripts/boot-capture.sh.
MOUSE_DX = 120
MOUSE_DY = -60

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


def read_screen(path: Path):
    """Sort every non black pixel by what drew it: text, rectangle, or cursor."""
    width, height, pixels = read_ppm(path)
    rows: dict[int, set[int]] = {}
    rect: set[tuple[int, int]] = set()
    cursor: set[tuple[int, int]] = set()

    for index in range(0, len(pixels), 3):
        colour = (pixels[index], pixels[index + 1], pixels[index + 2])
        if colour == (0, 0, 0):
            continue
        pixel = index // 3
        x, y = pixel % width, pixel // width
        if colour == (255, 255, 255):
            rows.setdefault(y, set()).add(x)
        elif colour == RECT_COLOUR:
            rect.add((x, y))
        elif colour == CURSOR_COLOUR:
            cursor.add((x, y))
        else:
            raise CheckFailed(
                f"{path.name}: pixel at ({x}, {y}) is rgb{colour}; expected black, "
                f"white text, the M3 rectangle rgb{RECT_COLOUR}, or the M4 cursor "
                f"rgb{CURSOR_COLOUR}"
            )

    if not rows:
        raise CheckFailed(f"{path.name}: no white text on the screen")
    return width, height, rows, rect, cursor


def find_bands(path: Path):
    """Split the white pixels into horizontal bands of text."""
    width, height, rows, rect, cursor = read_screen(path)

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
    return width, height, bands, rect, cursor


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


def check_rectangle(rect: set[tuple[int, int]], width: int, height: int,
                    key_line: Band, name: str) -> str:
    """The M3 rectangle: solid, the expected size, centred, clear of the text."""
    if not rect:
        raise CheckFailed(f"{name}: the M3 rectangle is missing")

    xs = {x for x, _ in rect}
    ys = {y for _, y in rect}
    left, right, top, bottom = min(xs), max(xs), min(ys), max(ys)
    actual_w, actual_h = right - left + 1, bottom - top + 1

    if len(rect) != actual_w * actual_h:
        raise CheckFailed(
            f"{name}: the rectangle has holes, {len(rect)} pixels inside a "
            f"{actual_w}x{actual_h} box"
        )

    expected_w = width // RECT_WIDTH_DIVISOR
    expected_h = height // RECT_HEIGHT_DIVISOR
    if (actual_w, actual_h) != (expected_w, expected_h):
        raise CheckFailed(
            f"{name}: the rectangle is {actual_w}x{actual_h}, expected "
            f"{expected_w}x{expected_h}"
        )
    if abs((left + right) / 2 - width / 2) > 1:
        raise CheckFailed(f"{name}: the rectangle is not horizontally centred")
    if top <= key_line.bottom:
        raise CheckFailed(f"{name}: the rectangle overlaps or sits above the key line")
    if right >= width or bottom >= height:
        raise CheckFailed(f"{name}: the rectangle runs off the screen")
    return f"M3 rectangle: {actual_w}x{actual_h} at ({left}, {top}), solid and centred"


def cursor_corner(cursor: set[tuple[int, int]], name: str) -> tuple[int, int, int]:
    """Top left of the cursor, and how many pixels it is made of."""
    if not cursor:
        raise CheckFailed(f"{name}: the M4 cursor is missing")
    xs = {x for x, _ in cursor}
    ys = {y for _, y in cursor}
    return min(xs), min(ys), len(cursor)


def check_screen(path: Path, key_line_text: str):
    width, height, bands, rect, cursor = find_bands(path)
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
    notes.append("  " + check_rectangle(rect, width, height, key_line, path.name))

    left, top, pixels = cursor_corner(cursor, path.name)
    if left >= width or top >= height:
        raise CheckFailed(f"{path.name}: the cursor is outside the screen")
    notes.append(f"  M4 cursor: {pixels} pixels, top left ({left}, {top})")
    return notes, (left, top, pixels), (width, height)


def check_log() -> list[str]:
    notes = []
    expected = (
        "kernel entered",
        "drew the M1 message",
        "drew the M3 rectangle",
        "keyboard ready",
        "mouse ready",
        "drew the M4 cursor",
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
        notes.append(
            f"{name} log: boot, message, rectangle, cursor, mouse and keyboard ready, "
            f"key {KEY_SENT} received")
    return notes


def check_cursor_movement(start, moved, clamped, size) -> list[str]:
    """The cursor tracks the mouse, keeps its shape, and stops at the edge."""
    width, height = size
    expected_x = width // CURSOR_START_X_DIVISOR
    expected_y = height // CURSOR_START_Y_DIVISOR

    if (start[0], start[1]) != (expected_x, expected_y):
        raise CheckFailed(
            f"the cursor started at {start[:2]}, expected ({expected_x}, {expected_y})"
        )
    if moved[2] != start[2] or clamped[2] != start[2]:
        raise CheckFailed(
            f"the cursor changed shape: {start[2]}, {moved[2]}, then {clamped[2]} pixels"
        )

    actual = (moved[0] - start[0], moved[1] - start[1])
    if actual != (MOUSE_DX, MOUSE_DY):
        raise CheckFailed(
            f"the mouse moved ({MOUSE_DX}, {MOUSE_DY}) but the cursor moved {actual}"
        )

    # The emulator caps how far one packet can move the pointer, so a hard
    # shove travels a long way without necessarily reaching the edge. What this
    # proves is that repeated movement keeps working and stays on screen.
    # Clamping itself is checked exactly in tests/pointer_test.c.
    if clamped[0] >= width or clamped[1] >= height:
        raise CheckFailed("the cursor left the screen")
    if clamped[0] <= moved[0] or clamped[1] <= moved[1]:
        raise CheckFailed("shoving the mouse toward the corner did not move the cursor")

    return [
        f"  cursor started at ({start[0]}, {start[1]}) and followed the mouse exactly",
        f"  shoved toward the corner it reached ({clamped[0]}, {clamped[1]}), still "
        f"whole and still inside the {width}x{height} screen",
    ]


def main() -> int:
    try:
        notes, boot_cursor, size = check_screen(SCREEN_BOOT, M2_PROMPT)
        key_notes, key_cursor, _ = check_screen(SCREEN_KEY, M2_AFTER_KEY)
        mouse_notes, moved_cursor, _ = check_screen(SCREEN_MOUSE, M2_AFTER_KEY)
        clamp_notes, clamped_cursor, _ = check_screen(SCREEN_CLAMP, M2_AFTER_KEY)
        notes += key_notes + mouse_notes + clamp_notes

        if key_cursor[:2] != boot_cursor[:2]:
            raise CheckFailed("the cursor moved on its own before the mouse was touched")

        notes += check_cursor_movement(boot_cursor, moved_cursor, clamped_cursor, size)
        notes += check_log()
    except CheckFailed as exc:
        print(f"check FAILED: {exc}")
        return 1

    for note in notes:
        print(f"  {note}")
    print("M1 to M4 checks passed: message, key press, rectangle, and a cursor that "
          "follows the mouse")
    print("A person should still watch it boot once with make run.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
