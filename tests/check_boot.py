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
  M5  the rectangle crossing the screen over time, staying whole and on screen
  M6  a sum typed on the keyboard, evaluated, and shown with its result
  M7  two conditionals that differ only in their comparison, taking opposite
      branches and showing different answers
  M8  a value stored under a name, then used in a sum and in a conditional on
      later lines, which only works if it was really remembered
  M9  the rectangle steered with the arrow keys, moving exactly as far as the
      presses say and stopping inside the corridor it is allowed
  M10 the steered rectangle wrapping across both corridor edges while remaining
      whole and on screen
  M11 a press inside the rectangle preserving its offset while held, and a
      pointer move after release leaving the rectangle behind
  M12 a triangle turning about its own centre, drawn with floating point:
      present in every capture, whole, on screen, and in a different position
      each time
  M14 two overlapping opaque window surfaces, with all existing Demo drawing
      in local coordinates and a compositor-owned desktop background

This is not a substitute for a person seeing it once on real hardware. It is
what keeps the milestones from silently breaking afterwards.

Run through `make test`.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = Path(os.environ.get("BUILD_DIR", "build"))
if not BUILD_DIR.is_absolute():
    BUILD_DIR = ROOT / BUILD_DIR
SCREEN_BOOT = BUILD_DIR / "screen-boot.ppm"
SCREEN_KEY = BUILD_DIR / "screen-key.ppm"
SCREEN_MOUSE = BUILD_DIR / "screen-mouse.ppm"
SCREEN_CLAMP = BUILD_DIR / "screen-clamp.ppm"
SCREEN_SUM = BUILD_DIR / "screen-sum.ppm"
SCREEN_POWER = BUILD_DIR / "screen-power.ppm"
SCREEN_TRUE = BUILD_DIR / "screen-true.ppm"
SCREEN_FALSE = BUILD_DIR / "screen-false.ppm"
SCREEN_ASSIGN = BUILD_DIR / "screen-assign.ppm"
SCREEN_VAR = BUILD_DIR / "screen-var.ppm"
SCREEN_VARIF = BUILD_DIR / "screen-varif.ppm"
SCREEN_STEER_DOWN = BUILD_DIR / "screen-steer-down.ppm"
SCREEN_STEER_LEFT = BUILD_DIR / "screen-steer-left.ppm"
SCREEN_WRAP_DOWN = BUILD_DIR / "screen-wrap-down.ppm"
SCREEN_WRAP_LEFT = BUILD_DIR / "screen-wrap-left.ppm"
SCREEN_DRAG_READY = BUILD_DIR / "screen-drag-ready.ppm"
SCREEN_DRAG_HELD = BUILD_DIR / "screen-drag-held.ppm"
SCREEN_DRAG_RELEASE = BUILD_DIR / "screen-drag-release.ppm"
DEBUG_LOG = BUILD_DIR / "debug.log"
SERIAL_LOG = BUILD_DIR / "serial.log"

# These mirror kernel/src/main.c. If the kernel's wording changes, change it here.
M1_MESSAGE = "IF YOU SEE THIS IT WORKED"
M2_PROMPT = "PRESS A KEY"
KEY_SENT = "A"
M2_AFTER_KEY = f"LAST KEY {KEY_SENT}"
# M6. These mirror scripts/boot-capture.sh, which types 12 + 30 and presses
# enter, and kernel/src/calc.c, which owns the prompt.
M6_PROMPT = "TYPE A SUM"
M6_SUM = "12+30=42"
M6_POWER = "2^5=32"
# M7. Both are typed in full; only the comparison differs, and so does the answer.
M7_TRUE = "IF 3>2 THEN 10 ELSE 20=10"
M7_FALSE = "IF 2>3 THEN 10 ELSE 20=20"
# M8. Three separate lines: the second and third only work if the first one's
# value survived them being typed.
M8_ASSIGN = "X=5=5"
M8_VAR = "X+3=8"
M8_VARIF = "IF X>2 THEN 10 ELSE 20=10"
# M9. The key line reports the last key, so after an arrow it names that arrow.
M2_AFTER_ARROW_DOWN = "LAST KEY DOWN"
M2_AFTER_ARROW_LEFT = "LAST KEY LEFT"
M6_AFTER_ENTER = "LAST KEY ENTER"
# Every letter types since M8, so the key injected to prove M2 lands on the sum
# line as well as on the key line, and stays there until escape clears it.
M8_KEY_ON_SUM_LINE = KEY_SENT

# These mirror kernel/src/main.c too.
RECT_COLOUR = (60, 170, 220)
RECT_WIDTH_DIVISOR = 4
RECT_HEIGHT_DIVISOR = 14

DEMO_X = 40
DEMO_Y = 40
DEMO_WIDTH = 1180
DEMO_HEIGHT = 720
SYSTEM_X = 860
SYSTEM_Y = 80
SYSTEM_WIDTH = 300
SYSTEM_HEIGHT = 180
DESKTOP_COLOUR = (18, 24, 38)
SYSTEM_COLOUR = (34, 46, 70)
ACCENT_COLOUR = (82, 190, 220)

CURSOR_COLOUR = (255, 214, 64)

# M12. These mirror kernel/src/main.c and kernel/src/geometry.c.
TRIANGLE_COLOUR = (80, 220, 120)
TRIANGLE_CENTRE_X_DIVISOR = 2
TRIANGLE_CENTRE_Y_PARTS = 7
TRIANGLE_CENTRE_Y_DIVISOR = 8
TRIANGLE_RADIUS_DIVISOR = 12
# 800 / 12 / 8, the wobble allowed in where the drawn outline averages to.
TRIANGLE_RADIUS_ALLOWANCE = 8

# Every triangle seen, in capture order. Filled in by check_screen so the run
# can compare one capture against the next without every caller carrying it.
TRIANGLES: list[tuple[str, tuple[float, float], frozenset]] = []

# M9. Where the rectangle was in each capture, in order, so movement can be
# measured from one to the next. Mirrors M9_STEP in kernel/src/main.c.
RECTANGLES: list[tuple[str, int, int]] = []
STEER_STEP = 16
STEER_DOWN_PRESSES = 3
STEER_LEFT_PRESSES = 8
WRAP_LEFT_PRESSES = 23
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

    def __init__(self, rows: dict[int, set[int]]) -> None:
        self.top = min(rows)
        self.bottom = max(rows)
        self.height = self.bottom - self.top + 1
        self.columns: set[int] = set()
        for columns in rows.values():
            self.columns |= columns
        self.left = min(self.columns)
        self.right = max(self.columns)
        # Every lit pixel, not just every lit column. Two lines can light the
        # same columns and still look nothing alike, which is exactly what
        # happens with X=5=5 and X+3=8.
        self.ink = sum(len(columns) for columns in rows.values())

    @property
    def centre_x(self) -> float:
        return (self.left + self.right) / 2

    @property
    def centre_y(self) -> float:
        return (self.top + self.bottom) / 2

    @property
    def word_gaps(self) -> int:
        """Gaps wide enough to be a space rather than the gap between glyphs."""
        columns = sorted(self.columns)
        if len(columns) < 2:
            return 0
        # A glyph cell is eight pixels at scale one; the gap inside a word is
        # three. Anything wider than half a cell of scaled space is a word gap.
        scale = max(1, self.height // INK_HEIGHT)
        threshold = 5 * scale
        return sum(1 for a, b in zip(columns, columns[1:]) if b - a > threshold)

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


def check_window_layout(path: Path) -> str:
    """M14: prove the desktop and both opaque surfaces occupy their own regions."""
    width, height, pixels = read_ppm(path)

    def colour_at(x: int, y: int) -> tuple[int, int, int]:
        if not (0 <= x < width and 0 <= y < height):
            raise CheckFailed(f"{path.name}: M14 sample ({x}, {y}) is off screen")
        index = (y * width + x) * 3
        return pixels[index], pixels[index + 1], pixels[index + 2]

    samples = (
        ((0, 0), DESKTOP_COLOUR, "desktop corner"),
        ((DEMO_X - 1, DEMO_Y + 100), DESKTOP_COLOUR, "desktop beside Demo"),
        ((DEMO_X + 10, DEMO_Y + 100), (0, 0, 0), "Demo local background"),
        ((SYSTEM_X, SYSTEM_Y), ACCENT_COLOUR, "System accent bar"),
        ((SYSTEM_X + 10, SYSTEM_Y + 40), SYSTEM_COLOUR, "System body"),
    )
    for point, expected, label in samples:
        actual = colour_at(*point)
        if actual != expected:
            raise CheckFailed(
                f"{path.name}: {label} at {point} is rgb{actual}, expected rgb{expected}"
            )

    if not (DEMO_X < SYSTEM_X < DEMO_X + DEMO_WIDTH and
            DEMO_Y < SYSTEM_Y < DEMO_Y + DEMO_HEIGHT):
        raise CheckFailed("M14 fixture no longer places System over Demo")

    return (
        f"M14 compositor: desktop background, Demo {DEMO_WIDTH}x{DEMO_HEIGHT} "
        f"at ({DEMO_X}, {DEMO_Y}), and System {SYSTEM_WIDTH}x{SYSTEM_HEIGHT} "
        f"opaque above it at ({SYSTEM_X}, {SYSTEM_Y})"
    )


def read_screen(path: Path):
    """Sort pixels drawn by the Demo app while accepting compositor colours."""
    width, height, pixels = read_ppm(path)
    rows: dict[int, set[int]] = {}
    rect: set[tuple[int, int]] = set()
    cursor: set[tuple[int, int]] = set()
    triangle: set[tuple[int, int]] = set()

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
        elif colour == TRIANGLE_COLOUR:
            triangle.add((x, y))
        elif colour in (DESKTOP_COLOUR, SYSTEM_COLOUR, ACCENT_COLOUR):
            continue
        else:
            raise CheckFailed(
                f"{path.name}: pixel at ({x}, {y}) is unexpected rgb{colour}"
            )

    if not rows:
        raise CheckFailed(f"{path.name}: no white text on the screen")
    return width, height, rows, rect, cursor, triangle


def find_bands(path: Path):
    """Split the white pixels into horizontal bands of text."""
    width, height, rows, rect, cursor, triangle = read_screen(path)

    bands: list[Band] = []
    current: dict[int, set[int]] = {}
    previous = None
    for row in sorted(rows):
        if previous is not None and row != previous + 1:
            bands.append(Band(current))
            current = {}
        current[row] = rows[row]
        previous = row
    bands.append(Band(current))
    return width, height, bands, rect, cursor, triangle


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
    # Two different lines can have the same number of glyphs. The spaces
    # between words are what tells TYPE A SUM apart from 12+30=42.
    spaces = text.count(" ")
    if band.word_gaps != spaces:
        raise CheckFailed(
            f"{name}: the {label} line has {band.word_gaps} word gaps, "
            f"expected {spaces} for {text!r}"
        )
    return (f"{label}: {band.glyphs} glyphs, {spaces} spaces, "
            f"scale {band.height // INK_HEIGHT}")


def check_rectangle(rect: set[tuple[int, int]], width: int, height: int,
                    key_line: Band, name: str,
                    cursor: set[tuple[int, int]] | None = None) -> str:
    """The M3 rectangle: solid, the expected size, and clear of the text.

    Solid except where the cursor is on top of it. Before M9 the rectangle only
    ever slid along one line and never met the cursor; now it can be steered
    underneath it, and the cursor is drawn last, so the hole it leaves is the
    system working. Anything missing outside the cursor is not.
    """
    if not rect:
        raise CheckFailed(f"{name}: the M3 rectangle is missing")

    xs = {x for x, _ in rect}
    ys = {y for _, y in rect}
    left, right, top, bottom = min(xs), max(xs), min(ys), max(ys)
    actual_w, actual_h = right - left + 1, bottom - top + 1

    if len(rect) != actual_w * actual_h:
        # One pixel of margin around the cursor, for the black outline drawn
        # just outside the arrow.
        if cursor:
            cursor_left = min(x for x, _ in cursor) - 1
            cursor_right = max(x for x, _ in cursor) + 1
            cursor_top = min(y for _, y in cursor) - 1
            cursor_bottom = max(y for _, y in cursor) + 1
        else:
            cursor_left = cursor_right = cursor_top = cursor_bottom = None

        missing = [
            (x, y)
            for y in range(top, bottom + 1)
            for x in range(left, right + 1)
            if (x, y) not in rect
        ]
        stray = [
            (x, y) for x, y in missing
            if cursor_left is None
            or not (cursor_left <= x <= cursor_right and cursor_top <= y <= cursor_bottom)
        ]
        if stray:
            raise CheckFailed(
                f"{name}: the rectangle has {len(stray)} holes in it that the cursor "
                f"does not explain, the first at {stray[0]}"
            )

    expected_w = DEMO_WIDTH // RECT_WIDTH_DIVISOR
    expected_h = DEMO_HEIGHT // RECT_HEIGHT_DIVISOR
    if (actual_w, actual_h) != (expected_w, expected_h):
        raise CheckFailed(
            f"{name}: the rectangle is {actual_w}x{actual_h}, expected "
            f"{expected_w}x{expected_h}"
        )
    # No centring check: since M5 the rectangle moves, so where it is depends
    # on when the screen was captured. That it moves is checked across
    # captures, in check_rectangle_movement.
    if left < DEMO_X or right >= DEMO_X + DEMO_WIDTH:
        raise CheckFailed(f"{name}: the rectangle escaped the Demo surface horizontally")
    if top <= key_line.bottom:
        raise CheckFailed(f"{name}: the rectangle overlaps or sits above the key line")
    if bottom >= DEMO_Y + DEMO_HEIGHT:
        raise CheckFailed(f"{name}: the rectangle escaped the Demo surface vertically")
    covered = actual_w * actual_h - len(rect)
    behind = f", {covered} pixels behind the cursor" if covered else ""
    return (f"M3 rectangle: {actual_w}x{actual_h} at ({left}, {top}), solid and on "
            f"screen{behind}")


def cursor_corner(cursor: set[tuple[int, int]], name: str) -> tuple[int, int, int]:
    """Top left of the cursor, and how many pixels it is made of."""
    if not cursor:
        raise CheckFailed(f"{name}: the M4 cursor is missing")
    xs = {x for x, _ in cursor}
    ys = {y for _, y in cursor}
    return min(xs), min(ys), len(cursor)


def check_triangle(triangle: set[tuple[int, int]], width: int, height: int,
                   name: str) -> tuple[str, tuple[float, float], frozenset]:
    """M12: drawn, whole, on screen, the right size, and about its centre."""
    if not triangle:
        raise CheckFailed(f"{name}: the M12 triangle is missing")

    xs = [x for x, _ in triangle]
    ys = [y for _, y in triangle]
    left, right, top, bottom = min(xs), max(xs), min(ys), max(ys)

    if left < 0 or top < 0 or right >= width or bottom >= height:
        raise CheckFailed(f"{name}: the triangle is not fully on screen")

    expected_x = DEMO_X + DEMO_WIDTH // TRIANGLE_CENTRE_X_DIVISOR
    expected_y = (DEMO_Y +
                  DEMO_HEIGHT * TRIANGLE_CENTRE_Y_PARTS //
                  TRIANGLE_CENTRE_Y_DIVISOR)
    radius = DEMO_HEIGHT // TRIANGLE_RADIUS_DIVISOR

    # The centre is the average of the drawn pixels, not the middle of the
    # bounding box. A triangle's box is not centred on the shape: as it turns,
    # the box slides about by a quarter of the radius even though the triangle
    # has not moved at all. The average of an evenly drawn outline does not.
    centre = (sum(xs) / len(xs), sum(ys) / len(ys))
    if abs(centre[0] - expected_x) > radius * 0.5:
        raise CheckFailed(
            f"{name}: the triangle is at x={centre[0]:.0f}, expected about {expected_x}")
    if abs(centre[1] - expected_y) > radius * 0.6:
        raise CheckFailed(
            f"{name}: the triangle is at y={centre[1]:.0f}, expected about {expected_y}")

    # It has to fit inside the circle it was given, with a little slack for
    # rounding and for the thickness of the line itself.
    span = max(right - left, bottom - top)
    if span > 2 * radius + 4:
        raise CheckFailed(
            f"{name}: the triangle spans {span} pixels, more than a circle of "
            f"radius {radius} allows")
    if span < radius:
        raise CheckFailed(f"{name}: the triangle spans only {span} pixels, too small")

    note = (f"M12 triangle: {len(triangle)} pixels, {span} across, centred near "
            f"({centre[0]:.0f}, {centre[1]:.0f})")
    return note, centre, frozenset(triangle)


def check_screen(path: Path, key_line_text: str, sum_line_text: str = M6_PROMPT):
    width, height, bands, rect, cursor, triangle = find_bands(path)
    notes = [f"{path.name}: {width}x{height}, {len(bands)} text lines"]

    if len(bands) != 3:
        raise CheckFailed(
            f"{path.name}: expected three lines, the M6 sum line, the M1 message "
            f"and the key line, found {len(bands)}"
        )

    sum_line, message, key_line = bands
    notes.append("  " + check_line(sum_line, sum_line_text, "M6 sum line", path.name))
    notes.append("  " + check_line(message, M1_MESSAGE, "M1 message", path.name))
    notes.append("  " + check_line(key_line, key_line_text, "M2 key line", path.name))

    if sum_line.bottom >= message.top:
        raise CheckFailed(f"{path.name}: the sum line is not above the M1 message")

    scale = message.height // INK_HEIGHT
    if abs(message.centre_x - width / 2) > FONT_WIDTH * scale:
        raise CheckFailed(f"{path.name}: the M1 message is not horizontally centred")
    if abs(message.centre_y - height / 2) > FONT_HEIGHT * scale:
        raise CheckFailed(f"{path.name}: the M1 message is not vertically centred")
    if key_line.top <= message.bottom:
        raise CheckFailed(f"{path.name}: the key line is not below the M1 message")
    notes.append("  M1 message centred, key line below it")
    notes.append("  " + check_rectangle(rect, width, height, key_line, path.name, cursor))

    left, top, pixels = cursor_corner(cursor, path.name)
    if left >= width or top >= height:
        raise CheckFailed(f"{path.name}: the cursor is outside the screen")
    notes.append(f"  M4 cursor: {pixels} pixels, top left ({left}, {top})")

    triangle_note, triangle_centre, triangle_pixels = check_triangle(
        triangle, width, height, path.name)
    notes.append("  " + triangle_note)
    TRIANGLES.append((path.name, triangle_centre, triangle_pixels))

    rect_left = min(x for x, _ in rect)
    RECTANGLES.append((path.name, rect_left, min(y for _, y in rect)))
    return notes, (left, top, pixels), (width, height), rect_left, sum_line.ink


def check_log() -> list[str]:
    notes = []
    expected = (
        "kernel entered",
        "window model ready, created IDs 1 and 2",
        "M14 compositor ready with two overlapping windows",
        "drew the M1 message",
        "drew the M3 rectangle",
        "keyboard ready",
        "mouse ready",
        "timer ready",
        "drew the M4 cursor",
        "drew the M6 sum line",
        "the rectangle is being steered",
        "rectangle moved to",
        "rectangle drag started",
        "rectangle dragged to",
        "rectangle drag ended",
        "floating point ready, drew the M12 triangle",
        f"key {KEY_SENT}",
        "sum 12+30 = 42",
        "sum 2^5 = 32",
        "sum IF 3>2 THEN 10 ELSE 20 = 10",
        "sum IF 2>3 THEN 10 ELSE 20 = 20",
        "sum X=5 = 5",
        "sum X+3 = 8",
        "sum IF X>2 THEN 10 ELSE 20 = 10",
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
            f"{name} log: boot, message, rectangle, cursor, sum line, key "
            f"{KEY_SENT} received, and the kernel's own answers for 12+30, 2^5, "
            f"both conditionals, and X stored then used twice")
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
    if moved[2] != start[2]:
        raise CheckFailed(
            f"the cursor changed shape while moving: {start[2]} pixels, then {moved[2]}"
        )
    # At the edge the arrow is partly off screen and so partly clipped, which
    # is the whole point of clamping. It must still be there, and it must not
    # have grown.
    if not 0 < clamped[2] <= start[2]:
        raise CheckFailed(
            f"after being shoved at the corner the cursor has {clamped[2]} pixels, "
            f"expected between 1 and {start[2]}"
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
        f"  shoved toward the corner it reached ({clamped[0]}, {clamped[1]}), "
        f"{clamped[2]} of its {start[2]} pixels still on a {width}x{height} screen",
    ]


def check_rectangle_movement(positions, size) -> list[str]:
    """M5: the rectangle crosses the screen, and never leaves it."""
    if len(set(positions)) == 1:
        raise CheckFailed(
            f"the rectangle sat at x={positions[0]} in every capture; it should be moving"
        )
    limit = DEMO_X + DEMO_WIDTH - DEMO_WIDTH // RECT_WIDTH_DIVISOR
    if any(x < DEMO_X or x > limit for x in positions):
        raise CheckFailed(f"the rectangle left the Demo surface: {positions}")
    travelled = max(positions) - min(positions)
    return [f"  M5 rectangle moved across {travelled} pixels: {positions}"]


def check_steering() -> list[str]:
    """M9: the arrow keys move the rectangle, by exactly as much as was pressed.

    The last three captures are the one before any arrow was pressed, the one
    after three presses of down, and the one after eight of left. Because the
    first arrow press also stops the rectangle drifting, the only thing that can
    move it afterwards is another press, so the distances are exact rather than
    approximate.
    """
    if len(RECTANGLES) < 3:
        raise CheckFailed("not enough captures to tell whether steering works")

    before, down, left = RECTANGLES[-3], RECTANGLES[-2], RECTANGLES[-1]

    fell = down[2] - before[2]
    if fell != STEER_STEP * STEER_DOWN_PRESSES:
        raise CheckFailed(
            f"{STEER_DOWN_PRESSES} presses of down moved the rectangle {fell} pixels, "
            f"expected {STEER_STEP * STEER_DOWN_PRESSES}")

    moved = left[1] - down[1]
    if moved != -STEER_STEP * STEER_LEFT_PRESSES:
        raise CheckFailed(
            f"{STEER_LEFT_PRESSES} presses of left moved the rectangle {moved} pixels, "
            f"expected {-STEER_STEP * STEER_LEFT_PRESSES}")

    if left[2] != down[2]:
        raise CheckFailed(
            f"pressing left changed the rectangle's height on screen, from {down[2]} "
            f"to {left[2]}; it should only have moved sideways")

    return [
        f"  M9 rectangle steered: down {STEER_DOWN_PRESSES} presses moved it {fell} "
        f"pixels, left {STEER_LEFT_PRESSES} presses moved it {-moved}, and nothing "
        f"else moved it once it was being steered",
    ]


def check_wrapping(size) -> list[str]:
    """M10: crossing each corridor edge reappears at the opposite edge."""
    if len(RECTANGLES) < 3:
        raise CheckFailed("not enough captures to tell whether wrapping works")

    before, wrapped_down, wrapped_left = RECTANGLES[-3:]
    if wrapped_down[1] != before[1]:
        raise CheckFailed(
            f"pressing down while wrapping changed x from {before[1]} to "
            f"{wrapped_down[1]}")
    if wrapped_down[2] >= before[2]:
        raise CheckFailed(
            f"crossing the lower corridor edge moved y from {before[2]} to "
            f"{wrapped_down[2]}; it should reappear near the top")

    span = DEMO_WIDTH - DEMO_WIDTH // RECT_WIDTH_DIVISOR + 1
    expected_x = DEMO_X + (
        wrapped_down[1] - DEMO_X - STEER_STEP * WRAP_LEFT_PRESSES
    ) % span
    if wrapped_left[1] != expected_x:
        raise CheckFailed(
            f"{WRAP_LEFT_PRESSES} presses of left from x={wrapped_down[1]} "
            f"landed at {wrapped_left[1]}, expected wrapped x={expected_x}")
    if wrapped_left[2] != wrapped_down[2]:
        raise CheckFailed(
            f"pressing left while wrapping changed y from {wrapped_down[2]} to "
            f"{wrapped_left[2]}")

    return [
        f"  M10 rectangle wrapped down from y={before[2]} to y={wrapped_down[2]}, "
        f"then left from x={wrapped_down[1]} to x={wrapped_left[1]} while "
        f"remaining whole and on screen",
    ]


def check_dragging(ready_cursor, held_cursor, released_cursor, size) -> list[str]:
    """M11: the held rectangle follows the pointer, then stays on release."""
    if len(RECTANGLES) < 3:
        raise CheckFailed("not enough captures to tell whether dragging works")

    ready, held, released = RECTANGLES[-3:]
    rect_width = DEMO_WIDTH // RECT_WIDTH_DIVISOR
    rect_height = DEMO_HEIGHT // RECT_HEIGHT_DIVISOR
    if not (ready[1] <= ready_cursor[0] < ready[1] + rect_width and
            ready[2] <= ready_cursor[1] < ready[2] + rect_height):
        raise CheckFailed(
            f"the M11 press point {ready_cursor[:2]} is outside the rectangle at "
            f"({ready[1]}, {ready[2]})")

    pointer_delta = (held_cursor[0] - ready_cursor[0],
                     held_cursor[1] - ready_cursor[1])
    rectangle_delta = (held[1] - ready[1], held[2] - ready[2])
    if pointer_delta != (-180, 30):
        raise CheckFailed(
            f"the held pointer moved {pointer_delta}, expected (-180, 30)")
    if rectangle_delta != pointer_delta:
        raise CheckFailed(
            f"the pointer moved {pointer_delta} while the held rectangle moved "
            f"{rectangle_delta}; the press offset was not preserved")

    after_release = (released_cursor[0] - held_cursor[0],
                     released_cursor[1] - held_cursor[1])
    if after_release != (80, 0):
        raise CheckFailed(
            f"after release the pointer moved {after_release}, expected (80, 0)")
    if released[1:] != held[1:]:
        raise CheckFailed(
            f"after release the rectangle moved from {held[1:]} to {released[1:]}")

    return [
        f"  M11 rectangle followed a held pointer by {pointer_delta}, preserving "
        f"the press offset, then stayed at ({held[1]}, {held[2]}) after release",
    ]


def check_rotation() -> list[str]:
    """M12: the triangle turns, and turns about a fixed point.

    One screenshot proves a triangle was drawn. Several, taken seconds apart,
    prove it is turning: the pixels have to differ while the centre stays where
    it was put.
    """
    if len(TRIANGLES) < 2:
        raise CheckFailed("not enough captures to tell whether the triangle turns")

    shapes = {pixels for _, _, pixels in TRIANGLES}
    if len(shapes) == 1:
        raise CheckFailed(
            f"the triangle was drawn identically in all {len(TRIANGLES)} captures; "
            f"it should have turned between them")

    xs = [centre[0] for _, centre, _ in TRIANGLES]
    ys = [centre[1] for _, centre, _ in TRIANGLES]
    drift = max(max(xs) - min(xs), max(ys) - min(ys))

    # An eighth of the radius. The centre of the drawn outline wobbles by a few
    # pixels as the triangle turns, because the three edges land on slightly
    # different numbers of pixels at different angles. That is the drawing, not
    # the geometry: tests/geometry_test.c measures the real centre over four
    # hundred turns and finds it moves half a pixel. A triangle that was
    # actually drifting would move tens of pixels, not a handful.
    allowed = (TRIANGLE_RADIUS_ALLOWANCE)
    if drift > allowed:
        raise CheckFailed(
            f"the triangle's centre wandered {drift:.0f} pixels across the captures, "
            f"more than the {allowed} that drawing explains; it should turn about a "
            f"fixed point")

    return [
        f"  M12 triangle turned: {len(shapes)} different shapes across "
        f"{len(TRIANGLES)} captures, centre steady within {drift:.0f} pixels",
    ]


def main() -> int:
    try:
        notes, boot_cursor, size, rect_boot, sum_boot = check_screen(
            SCREEN_BOOT, M2_PROMPT)
        notes.append("  " + check_window_layout(SCREEN_BOOT))
        key_notes, key_cursor, _, rect_key, _ = check_screen(
            SCREEN_KEY, M2_AFTER_KEY, M8_KEY_ON_SUM_LINE)
        mouse_notes, moved_cursor, _, rect_mouse, _ = check_screen(
            SCREEN_MOUSE, M2_AFTER_KEY, M8_KEY_ON_SUM_LINE)
        clamp_notes, clamped_cursor, _, rect_clamp, sum_clamp = check_screen(
            SCREEN_CLAMP, M2_AFTER_KEY, M8_KEY_ON_SUM_LINE)
        typed_notes, _, _, rect_sum, sum_typed = check_screen(
            SCREEN_SUM, M6_AFTER_ENTER, M6_SUM)
        power_notes, _, _, rect_power, sum_power = check_screen(
            SCREEN_POWER, M6_AFTER_ENTER, M6_POWER)
        true_notes, _, _, rect_true, sum_true = check_screen(
            SCREEN_TRUE, M6_AFTER_ENTER, M7_TRUE)
        false_notes, _, _, rect_false, sum_false = check_screen(
            SCREEN_FALSE, M6_AFTER_ENTER, M7_FALSE)
        assign_notes, _, _, rect_assign, sum_assign = check_screen(
            SCREEN_ASSIGN, M6_AFTER_ENTER, M8_ASSIGN)
        var_notes, _, _, rect_var, sum_var = check_screen(
            SCREEN_VAR, M6_AFTER_ENTER, M8_VAR)
        varif_notes, _, _, rect_varif, sum_varif = check_screen(
            SCREEN_VARIF, M6_AFTER_ENTER, M8_VARIF)
        notes += (key_notes + mouse_notes + clamp_notes + typed_notes + power_notes
                  + true_notes + false_notes + assign_notes + var_notes
                  + varif_notes)

        if sum_typed == sum_clamp:
            raise CheckFailed(
                "the sum line looks identical before and after typing; it should "
                "have changed from the prompt to the sum and its result"
            )
        if sum_power == sum_typed:
            raise CheckFailed(
                "the sum line did not change between the two sums; the second "
                "one should have replaced the first"
            )
        notes.append(
            f"  M6 sum line changed from {sum_clamp} lit pixels to {sum_typed} "
            f"after typing {M6_SUM.split('=')[0]}, then to {sum_power} after "
            f"{M6_POWER.split('=')[0]}, which needs a shifted key")

        # The two conditionals are the same length and the same shape. What
        # tells them apart is the comparison and the answer, so the drawn line
        # has to differ.
        if sum_true == sum_false:
            raise CheckFailed(
                "both conditionals drew the same line; they should have taken "
                "opposite branches and shown different answers"
            )
        notes.append(
            f"  M7 conditionals drew different lines, {sum_true} and {sum_false} "
            f"lit pixels: {M7_TRUE} and {M7_FALSE}")

        # X=5=5 and X+3=8 are both five glyphs with no spaces, so glyph
        # counting alone cannot tell them apart. What they draw must differ.
        if sum_assign == sum_var:
            raise CheckFailed(
                "storing X and then using it drew the same line; the second "
                "line should show the sum and its answer, not the assignment"
            )
        if sum_varif == sum_true:
            raise CheckFailed(
                "the conditional using X drew the same line as the one using 3; "
                "they differ by a character, so they should differ on screen"
            )
        notes.append(
            f"  M8 stored {M8_ASSIGN.split('=')[0]}=5, then used it on later "
            f"lines: {M8_VAR} and {M8_VARIF}, {sum_assign}, {sum_var} and "
            f"{sum_varif} lit pixels")

        if key_cursor[:2] != boot_cursor[:2]:
            raise CheckFailed("the cursor moved on its own before the mouse was touched")

        notes += check_cursor_movement(boot_cursor, moved_cursor, clamped_cursor, size)
        notes += check_rectangle_movement(
            [rect_boot, rect_key, rect_mouse, rect_clamp, rect_sum, rect_power,
             rect_true, rect_false, rect_assign, rect_var, rect_varif], size)
        steer_down_notes, _, _, rect_steer_down, _ = check_screen(
            SCREEN_STEER_DOWN, M2_AFTER_ARROW_DOWN, M8_VARIF)
        steer_left_notes, _, _, rect_steer_left, _ = check_screen(
            SCREEN_STEER_LEFT, M2_AFTER_ARROW_LEFT, M8_VARIF)
        notes += steer_down_notes + steer_left_notes

        notes += check_steering()
        wrap_down_notes, _, _, _, _ = check_screen(
            SCREEN_WRAP_DOWN, M2_AFTER_ARROW_DOWN, M8_VARIF)
        wrap_left_notes, _, _, _, _ = check_screen(
            SCREEN_WRAP_LEFT, M2_AFTER_ARROW_LEFT, M8_VARIF)
        notes += wrap_down_notes + wrap_left_notes
        notes += check_wrapping(size)
        drag_ready_notes, drag_ready_cursor, _, _, _ = check_screen(
            SCREEN_DRAG_READY, M2_AFTER_ARROW_LEFT, M8_VARIF)
        drag_held_notes, drag_held_cursor, _, _, _ = check_screen(
            SCREEN_DRAG_HELD, M2_AFTER_ARROW_LEFT, M8_VARIF)
        drag_release_notes, drag_release_cursor, _, _, _ = check_screen(
            SCREEN_DRAG_RELEASE, M2_AFTER_ARROW_LEFT, M8_VARIF)
        notes += drag_ready_notes + drag_held_notes + drag_release_notes
        notes += check_dragging(
            drag_ready_cursor, drag_held_cursor, drag_release_cursor, size)
        notes += check_rotation()
        notes += check_log()
    except CheckFailed as exc:
        print(f"check FAILED: {exc}")
        return 1

    for note in notes:
        print(f"  {note}")
    print("M1 to M14 checks passed: message, key press, a rectangle that drifts, "
          "can be steered, wraps and is dragged, a cursor that follows the mouse, sums "
          "answered, a conditional taking each branch in turn, a value remembered "
          "under a name and used again, a triangle turning about its own centre, and "
          "two opaque window surfaces composited over a desktop")
    print("A person should still watch it boot once with make run.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
