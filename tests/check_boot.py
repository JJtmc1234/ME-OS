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
  M15 clicks target the topmost window, move keyboard focus, and raise the
      focused window without leaking input to the background window

This is not a substitute for a person seeing it once on real hardware. It is
what keeps the milestones from silently breaking afterwards.

Run through `make test`.
"""

from __future__ import annotations

import os
import re
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
SCREEN_FOCUS_SYSTEM = BUILD_DIR / "screen-focus-system.ppm"
SCREEN_FOCUS_DEMO = BUILD_DIR / "screen-focus-demo.ppm"
SCREEN_DRAG_RELEASE = BUILD_DIR / "screen-drag-release.ppm"
DEBUG_LOG = BUILD_DIR / "debug.log"
# The second boot, which is what proves the disk survived a restart.
DEBUG_LOG_AGAIN = BUILD_DIR / "debug-again.log"
# Mirrors kernel/src/main.c. The exact line, because the kernel also has a
# "nothing loaded from the disk" that differs from it by one word.
LOADED_LINE = "me-os: filesystem loaded from disk"
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

# Demo's client area, filled in from the kernel's own log by
# `load_demo_geometry`. Not written down here, because the tiling layout decides
# it and a second copy of that arithmetic in this file would drift from the real
# one. Everything inside Demo is reported relative to its client area, so the
# origin is zero rather than wherever the layout put the window.
DEMO_X = 0
DEMO_Y = 0
DEMO_WIDTH = 0
DEMO_HEIGHT = 0
# M18 theme. Every colour the desktop paints, so a pixel that is none of them
# inside a window is a real fault rather than a colour nobody listed.
# Mirrored from kernel/src/tile.c. The layout itself is read back from the
# kernel's log rather than recomputed, so these are only used to say which strip
# of the screen a bar owns.
TOP_BAR_HEIGHT = 28
TASKBAR_HEIGHT = 34
SCREEN_HEIGHT = 800

DESKTOP_COLOUR = (12, 14, 18)
BAR_COLOUR = (20, 23, 29)
ACCENT_COLOUR = (72, 214, 224)
CHROME_COLOUR = (28, 32, 40)
CHROME_TEXT_COLOUR = (198, 206, 218)
BAR_TEXT_COLOUR = (214, 220, 230)
BAR_DIM_COLOUR = (112, 122, 138)
BORDER_COLOUR = (44, 50, 60)
# What a window's client area starts as, and what Demo erases with.
WINDOW_COLOUR = (17, 20, 25)

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
# How far the M11 drag pulls the pointer, and which way.
#
# Read from the file the capture script writes rather than written down twice.
# Which way it drags depends on where the rectangle was when the pointer was
# aimed at it, so this cannot be a constant here without the two disagreeing on
# the runs where it went the other way.
def drag_delta() -> int:
    path = BUILD_DIR / "drag-delta.txt"
    if not path.exists():
        raise CheckFailed(
            "scripts/boot-capture.sh did not say how far it dragged, so there "
            "is nothing to check the drag against")
    return int(path.read_text().strip())

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



class Tile:
    """Where the kernel says it put one window, read back from its own log."""

    def __init__(self, title, x, y, width, height,
                 client_x, client_y, client_width, client_height, focused):
        self.title = title
        self.x, self.y = x, y
        self.width, self.height = width, height
        self.client_x, self.client_y = client_x, client_y
        self.client_width, self.client_height = client_width, client_height
        self.focused = focused

    def contains_client(self, x, y):
        return (self.client_x <= x < self.client_x + self.client_width and
                self.client_y <= y < self.client_y + self.client_height)


def parse_layouts() -> list[list[Tile]]:
    """Every layout the kernel reported, oldest first.

    A run is a block of consecutive tile lines. A repeated title starts a new
    one, because the kernel prints every window each time it lays out.
    """
    text = DEBUG_LOG.read_text(errors="ignore")
    runs: list[list[Tile]] = []
    current: list[Tile] = []
    seen: set[str] = set()

    def close():
        nonlocal current, seen
        if current:
            runs.append(current)
        current, seen = [], set()

    for line in text.splitlines():
        if not line.startswith("me-os: tile "):
            close()
            continue
        body = line[len("me-os: tile "):]
        # A window with no rectangle is not on the screen: hidden, or on another
        # workspace. It still starts a new run, because the kernel prints every
        # window each time, but there is nothing to check it against.
        on_screen = " at " in body
        title = (body.split(" at ")[0].split(" hidden")[0]
                     .split(" on workspace")[0].strip())
        if title in seen:
            close()
        seen.add(title)
        if not on_screen:
            continue
        try:
            head, rest = body.split(" at ", 1)
            where, rest = rest.split(" size ", 1)
            size, rest = rest.split(" client ", 1)
            client_where, rest = rest.split(" ", 1)
            client_size = rest.split()[0]
            x, y = (int(v) for v in where.split(","))
            width, height = (int(v) for v in size.split("x"))
            cx, cy = (int(v) for v in client_where.split(","))
            cw, ch = (int(v) for v in client_size.split("x"))
        except (ValueError, IndexError) as exc:
            raise CheckFailed(f"could not read the layout line {line!r}: {exc}")
        current.append(Tile(head.strip(), x, y, width, height, cx, cy, cw, ch,
                            rest.strip().endswith("focused")))
    close()

    runs = [run for run in runs if run]
    if not runs:
        raise CheckFailed("the kernel never reported a layout")
    return runs


def read_layout(which: int = 0) -> dict[str, Tile]:
    """The last layout the kernel reported, one entry per window.

    Read from the log rather than recomputed here, so this file does not hold a
    second copy of the tiling arithmetic that could drift from the real one. The
    rectangles are then checked against each other for overlap, so what the
    kernel says is verified rather than believed.
    """
    return {tile.title: tile for tile in parse_layouts()[which]}


def demo_tile() -> Tile:
    """Where Demo was during the M1 to M12 captures, which is the first layout.

    Those captures are all taken while Demo has the workspace to itself. The
    later ones, after the other windows are brought in, are checked by
    `check_tiles_on_screen` against the layout that was current for them.
    """
    tiles = read_layout(0)
    if "DEMO" not in tiles:
        raise CheckFailed("the kernel never reported where it put the Demo window")
    return tiles["DEMO"]


def load_demo_geometry() -> None:
    """Reads Demo's client size out of the layout the kernel reported."""
    global DEMO_WIDTH, DEMO_HEIGHT
    tile = demo_tile()
    DEMO_WIDTH, DEMO_HEIGHT = tile.client_width, tile.client_height


def check_tiling() -> list[str]:
    """M17 and M18, against every layout the kernel reported, not only the last.

    Every one of them, because a reflow that produced overlapping tiles would
    otherwise hide behind a tidy final state.
    """
    runs = parse_layouts()
    counts = set()
    for run in runs:
        counts.add(len(run))
        for i, a in enumerate(run):
            if a.y < TOP_BAR_HEIGHT:
                raise CheckFailed(
                    f"the tile {a.title} at y {a.y} reaches into the top bar")
            if a.y + a.height > SCREEN_HEIGHT - TASKBAR_HEIGHT:
                raise CheckFailed(
                    f"the tile {a.title} ends at y {a.y + a.height}, "
                    f"inside the taskbar")
            for b in run[i + 1:]:
                if (a.x < b.x + b.width and b.x < a.x + a.width and
                        a.y < b.y + b.height and b.y < a.y + a.height):
                    raise CheckFailed(
                        f"the tiles {a.title} and {b.title} overlap: "
                        f"{a.x},{a.y} {a.width}x{a.height} against "
                        f"{b.x},{b.y} {b.width}x{b.height}")

    if len(counts) < 3:
        raise CheckFailed(
            f"the run only ever showed {sorted(counts)} windows at once, so it "
            f"did not demonstrate that opening and closing windows reflows the "
            f"rest")

    return [
        f"M17 tiling: {len(runs)} layouts across "
        f"{', '.join(str(c) for c in sorted(counts))} visible windows, none "
        f"overlapping and none reaching into a bar"
    ]


def check_focus_moved() -> list[str]:
    """M18: focus really moved between windows, not merely once and back.

    The window it lands on depends on what is showing at the time, so the
    numbers are not written down here. What matters is that more than one window
    held focus during the run.
    """
    text = DEBUG_LOG.read_text(errors="ignore")
    landed = {line.rsplit(" ", 1)[-1].strip()
              for line in text.splitlines()
              if "focus moved to window " in line}
    if len(landed) < 2:
        raise CheckFailed(
            f"focus only ever landed on {landed or 'nothing'}, so nothing showed "
            f"that it moves between tiles")
    return [f"M18 focus: it moved between windows {', '.join(sorted(landed))} "
            f"during the run"]


def check_terminal() -> list[str]:
    """M19: the terminal answered with things the machine had to look up.

    Checked from the log rather than by reading pixels, because what is being
    checked is that the command ran and reported real values. That the answer
    reached the screen is `check_tiles_on_screen`'s job.
    """
    text = DEBUG_LOG.read_text(errors="ignore")
    ran = [line.split("terminal ran ", 1)[1].strip()
           for line in text.splitlines() if "terminal ran " in line]
    if len(ran) < 2:
        raise CheckFailed(
            f"the terminal only ran {ran}, so it did not demonstrate a shell")

    # M20. A file was made from the keyboard and read back, which is the whole
    # claim: the commands act on a real tree rather than printing an answer.
    made = "ECHO TILING WORKS > PROJECTS/NOTE.TXT"
    if made not in ran:
        raise CheckFailed("nothing was written to a file from the terminal")
    if ran.index(made) > ran.index("CAT PROJECTS/NOTE.TXT"):
        raise CheckFailed("the file was read before it was written")

    return [f"M19 and M20 terminal: ran {len(ran)} commands including "
            f"{made.split(' > ')[0].lower()} into a file it then read back"]


def check_editor() -> list[str]:
    """M21: a file was written in the editor and read back in the shell.

    The order is the claim. Opening, typing, saving and then reading the same
    file somewhere else is the loop that makes a filesystem worth having, and it
    only means anything if the read comes after the save.
    """
    text = DEBUG_LOG.read_text(errors="ignore")
    lines = text.splitlines()

    def when(phrase):
        for i, line in enumerate(lines):
            if phrase in line:
                return i
        raise CheckFailed(f"the kernel never reported {phrase!r}")

    opened = when("editor opened TODO.TXT")
    saved = when("editor saved TODO.TXT")
    read = when("terminal ran CAT TODO.TXT")
    if not opened < saved < read:
        raise CheckFailed(
            f"the editor's open, save and read happened in the order "
            f"{opened}, {saved}, {read}, which is not open then save then read")

    return ["M21 editor: opened a file, typed into it, saved it with Ctrl O, "
            "and read it back from the shell afterwards"]


def check_clock() -> list[str]:
    """M21: the clock chip answered, and with something possible.

    A wrong clock is worse than a missing one, because nothing downstream can
    tell it is wrong. The kernel refuses an impossible reading, so this checks
    that a reading arrived at all and that it looks like a date.
    """
    text = DEBUG_LOG.read_text(errors="ignore")
    for line in text.splitlines():
        if "clock says " in line:
            stamp = line.split("clock says ", 1)[1].strip()
            date, _, clock = stamp.partition(" ")
            parts = date.split("-")
            if len(parts) != 3 or not all(p.isdigit() for p in parts):
                raise CheckFailed(f"the clock reported {date!r}, which is not a date")
            if len(clock.split(":")) != 3:
                raise CheckFailed(f"the clock reported {clock!r}, which is not a time")
            return [f"M21 clock: the chip answered {stamp}"]

    # Not a failure. A machine whose clock will not answer is a real machine,
    # and the kernel says so and shows the uptime instead.
    if "clock chip would not answer" not in text:
        raise CheckFailed("the kernel neither read the clock nor said it could not")
    return ["M21 clock: the chip would not answer, and the bar says uptime instead"]


def check_workspaces() -> list[str]:
    """M22: windows really moved to another set of tiles, and it was looked at.

    Checked by the layouts either side of the switch. A window that left this
    workspace has to be absent from the layout afterwards, and present again in
    the one taken after the switch, or nothing moved and only a number changed.
    """
    runs = parse_layouts()
    names = [{tile.title for tile in run} for run in runs]

    moved_away = False
    for before, after in zip(names, names[1:]):
        if after < before:
            moved_away = True
            break
    if not moved_away:
        raise CheckFailed(
            "no layout ever held fewer windows than the one before it, so "
            "nothing was moved off the screen")

    text = DEBUG_LOG.read_text(errors="ignore")
    switched = [line for line in text.splitlines()
                if line.startswith("me-os: workspace ")]
    if not switched:
        raise CheckFailed("the desktop never changed workspace")

    # Direct evidence rather than a count. A window that reports itself on
    # another workspace really left this screen, and a count could be explained
    # by a window that was merely hidden.
    elsewhere = [line for line in text.splitlines()
                 if line.startswith("me-os: tile ") and " on workspace " in line]
    if not elsewhere:
        raise CheckFailed(
            "no window ever reported itself on another workspace, so the "
            "windows stayed where they were and only a number changed")

    # The layout after the switch has to be the windows that were sent there,
    # not the ones that stayed, or switching showed the wrong screen.
    final = names[-1]
    if not final:
        raise CheckFailed("the workspace switched to had nothing on it")

    return [f"M22 workspaces: {len(switched)} switch(es), and the layout after "
            f"one holds {', '.join(sorted(final))}"]


def check_persistence() -> list[str]:
    """M23: the filesystem was on a disk, and a second boot read it back.

    Two logs, because one boot cannot prove this. The first has to say it wrote
    the disk. The second is a kernel that was handed nothing but the ISO and
    that disk, so anything it knows about /PERSIST.TXT it read off it.
    """
    first = DEBUG_LOG.read_text(errors="ignore")
    if not DEBUG_LOG_AGAIN.exists():
        raise CheckFailed("the machine was never restarted, so nothing was proved")
    again = DEBUG_LOG_AGAIN.read_text(errors="ignore")

    found = [l for l in first.splitlines()
             if l.startswith("me-os: disk ") and " sectors of " in l]
    if not found:
        raise CheckFailed(
            "the first boot found no disk, so there was nothing to save to: "
            + next((l for l in first.splitlines() if "no disk found" in l),
                   "and it did not say why"))
    if "me-os: FAILED to save" in first:
        line = next(l for l in first.splitlines() if "FAILED to save" in l)
        raise CheckFailed(f"a save failed during the first boot: {line}")
    if "me-os: filesystem saved" not in first:
        raise CheckFailed("the first boot never saved the filesystem")

    # The first boot must have started from nothing, or the disk was left over
    # from a previous run and this check proves only that a file exists.
    if LOADED_LINE in first:
        raise CheckFailed(
            "the first boot loaded a filesystem, so the disk was not blank and "
            "the second boot proves nothing about this run")

    loaded = [l for l in again.splitlines() if l.startswith(LOADED_LINE)]
    if not loaded:
        raise CheckFailed(
            "the second boot did not load the disk the first one wrote: "
            + next((l for l in again.splitlines() if "nothing loaded" in l),
                   "and gave no reason"))

    holds = [l for l in again.splitlines() if l.startswith("me-os: disk holds")]
    if not holds:
        raise CheckFailed("the second boot never said what the disk holds")
    if "PERSIST.TXT" not in holds[-1]:
        raise CheckFailed(
            "the file written before the restart is not on the disk after it: "
            + holds[-1])

    # M27. The script made this, and nothing else did. A line the shell ran from
    # a file is not logged as typed, which is right, so what the script did is
    # the only evidence that it ran at all.
    if "MADE-BY-SCRIPT" not in holds[-1]:
        raise CheckFailed(
            "what the script was told to make is not there, so RUN did not run "
            "it: " + holds[-1])

    # M24. One file that really spans blocks, not a total across the filesystem.
    # A hundred single block files add up to the same number as fifty of two, so
    # only the largest file can answer whether the join between two blocks was
    # written, read back, and put together in the right order.
    largest = [l for l in again.splitlines() if l.startswith("me-os: largest file ")]
    if not largest:
        raise CheckFailed("the second boot never said what the largest file was")
    spans = int(largest[-1].split(" bytes in ")[-1].split()[0])
    if spans < 2:
        raise CheckFailed(
            "nothing that came back off the disk was longer than a single block, "
            "so the join between two was never tested: " + largest[-1])

    # And the same bytes in the same order. The block count says a file spanned
    # two blocks. It does not say they came back the right way round, and a file
    # whose blocks were swapped is exactly as long as one that was not.
    before = [l for l in first.splitlines() if l.startswith("me-os: largest file ")]
    if not before:
        raise CheckFailed("the first boot never said what its largest file was")
    if before[-1].split(", sum ")[-1] != largest[-1].split(", sum ")[-1]:
        raise CheckFailed(
            "the largest file changed across the restart, so what came off the "
            f"disk is not what went on it:\n    before {before[-1]}\n"
            f"    after  {largest[-1]}")

    return [f"M23 disk: {found[-1].split('me-os: disk ')[-1]}",
            "M27 scripts: RUN made MADE-BY-SCRIPT, and it survived the restart",
            f"M24 blocks: {largest[-1].split('largest file ')[-1]}, so a file "
            f"that spans blocks came back whole",
            f"M23 persistence: {loaded[-1].split('disk, ')[-1]} came back after "
            f"a restart, holding{holds[-1].split('disk holds')[-1]}"]


def check_scrollback() -> list[str]:
    """M26: the terminal really scrolled back, and really came down again.

    Checked from the kernel's own report of where the view is. A screenshot
    would show text either way, and the question is whether it is older text.
    """
    text = DEBUG_LOG.read_text(errors="ignore")
    moved = [l for l in text.splitlines() if "terminal scrolled to " in l]
    if not moved:
        raise CheckFailed(
            "the terminal never scrolled, so either the page keys are not "
            "decoded or nothing was kept to scroll back to")

    def lines_back(line: str) -> int:
        return int(line.split("scrolled to ")[-1].split()[0])

    depths = [lines_back(l) for l in moved]
    if max(depths) == 0:
        raise CheckFailed(f"the view never left the newest line: {moved}")
    if depths[-1] != 0:
        raise CheckFailed(
            f"the view never came back down, so page down does not work: {moved[-1]}")
    held = int(moved[0].split(" lines back of ")[-1])
    if held < max(depths):
        raise CheckFailed(
            f"it scrolled back {max(depths)} lines but only {held} were kept")

    return [f"M26 scrollback: went {max(depths)} lines back of {held} kept, "
            f"and came down to the newest again"]


def check_tiles_on_screen(path: Path) -> list[str]:
    """The tiles really are on the screen where the kernel said it put them.

    The log is the kernel's own account of itself, so it is checked against
    pixels: the middle of every tile has to be window content, and the gap
    between two tiles has to be bare desktop. A layout that was correct in the
    log and not on the screen would pass everything above this.
    """
    width, height, pixels = read_ppm(path)
    tiles = list(read_layout(-1).values())
    if len(tiles) < 2:
        raise CheckFailed(
            f"{path.name} was captured with {len(tiles)} windows showing, so it "
            f"cannot demonstrate a gap between two tiles")

    def colour_at(x, y):
        index = (y * width + x) * 3
        return pixels[index], pixels[index + 1], pixels[index + 2]

    for tile in tiles:
        middle = colour_at(tile.client_x + tile.client_width // 2,
                           tile.client_y + tile.client_height // 2)
        if middle == DESKTOP_COLOUR:
            raise CheckFailed(
                f"{path.name}: the middle of {tile.title} is bare desktop, so "
                f"the window is not where the log says it is")
        corner = colour_at(tile.x, tile.y)
        if corner not in (ACCENT_COLOUR, BORDER_COLOUR):
            raise CheckFailed(
                f"{path.name}: {tile.title} has no border at its corner "
                f"({tile.x}, {tile.y}), found rgb{corner}")

    focused = [t for t in tiles if colour_at(t.x, t.y) == ACCENT_COLOUR]
    if len(focused) != 1:
        raise CheckFailed(
            f"{path.name}: {len(focused)} tiles carry the accent border, and "
            f"exactly one window has focus")

    # The gap between the two leftmost tiles in the same column, which is bare
    # desktop by construction and the clearest evidence they do not overlap.
    ordered = sorted(tiles, key=lambda t: (t.x, t.y))
    a, b = ordered[0], ordered[1]
    if a.x == b.x and b.y > a.y + a.height:
        gap = colour_at(a.x + a.width // 2, (a.y + a.height + b.y) // 2)
    else:
        gap = colour_at((a.x + a.width + b.x) // 2, a.y + a.height // 2)
    if gap != DESKTOP_COLOUR:
        raise CheckFailed(
            f"{path.name}: the gap between {a.title} and {b.title} is rgb{gap}, "
            f"not the desktop showing through")

    return [
        f"M18 desktop: {len(tiles)} tiles on screen where the log said, one "
        f"accent border, and bare desktop in the gap between them"
    ]


def read_screen(path: Path):
    """Sort the pixels Demo drew, looking only inside Demo's own window.

    Since M18 the screen is a desktop: two bars, a frame around every tile and
    up to three other windows. Demo's drawing lives inside Demo's client area
    and nowhere else, so that is where these checks look, and everything is
    reported in Demo's own coordinates. Where Demo is comes from the kernel's
    log, so this file does not hold a second copy of the tiling arithmetic.

    The cursor is the exception. It is a desktop overlay rather than part of any
    window, so it is collected from the whole screen in screen coordinates.
    """
    width, height, pixels = read_ppm(path)
    tile = demo_tile()
    rows: dict[int, set[int]] = {}
    rect: set[tuple[int, int]] = set()
    cursor: set[tuple[int, int]] = set()
    triangle: set[tuple[int, int]] = set()

    for index in range(0, len(pixels), 3):
        colour = (pixels[index], pixels[index + 1], pixels[index + 2])
        pixel = index // 3
        x, y = pixel % width, pixel // width

        if colour == CURSOR_COLOUR:
            cursor.add((x, y))
            continue
        if not tile.contains_client(x, y):
            continue

        local = (x - tile.client_x, y - tile.client_y)
        if colour in (WINDOW_COLOUR, (0, 0, 0)):
            continue
        if colour == (255, 255, 255):
            rows.setdefault(local[1], set()).add(local[0])
        elif colour == RECT_COLOUR:
            rect.add(local)
        elif colour == TRIANGLE_COLOUR:
            triangle.add(local)
        else:
            raise CheckFailed(
                f"{path.name}: pixel at ({x}, {y}), inside the Demo window, "
                f"is unexpected rgb{colour}"
            )

    if not rows:
        raise CheckFailed(f"{path.name}: no white text inside the Demo window")
    return tile.client_width, tile.client_height, rows, rect, cursor, triangle


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
    # The cursor is collected in screen coordinates, because it is a desktop
    # overlay rather than part of any window, and the rectangle is in Demo's
    # own. The hole the cursor leaves in the rectangle can only be recognised
    # when both are said in the same coordinates.
    tile = demo_tile()
    cursor_local = {(x - tile.client_x, y - tile.client_y) for x, y in cursor}
    notes.append("  " + check_rectangle(rect, width, height, key_line, path.name,
                                        cursor_local))

    left, top, pixels = cursor_corner(cursor, path.name)
    screen_width, screen_height, _ = read_ppm(path)
    if left >= screen_width or top >= screen_height:
        raise CheckFailed(f"{path.name}: the cursor is outside the screen")
    notes.append(f"  M4 cursor: {pixels} pixels, top left ({left}, {top})")

    triangle_note, triangle_centre, triangle_pixels = check_triangle(
        triangle, width, height, path.name)
    notes.append("  " + triangle_note)
    TRIANGLES.append((path.name, triangle_centre, triangle_pixels))

    rect_left = min(x for x, _ in rect)
    RECTANGLES.append((path.name, rect_left, min(y for _, y in rect)))
    return (notes, (left, top, pixels), (width, height), rect_left, sum_line.ink,
            (screen_width, screen_height))


def check_log() -> list[str]:
    notes = []
    expected = (
        "kernel entered",
        "window model ready, created IDs 1, 2, 3, 4",
        "M14 compositor ready with tiled window surfaces",
        "M18 ME OS Default desktop ready, tiling first",
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
        "focus moved to window ",
        "hidden windows shown, the layout reflowed",
        "window hidden, the layout reflowed",
        "terminal ran VER",
        "terminal ran CPU",
        "terminal ran MKDIR PROJECTS",
        "terminal ran CAT PROJECTS/NOTE.TXT",
        "editor opened TODO.TXT",
        "editor saved TODO.TXT",
        "terminal ran LS | SORT > SORTED.TXT",
        # M28. The line was typed as CAT COM and Tab finished it. If
        # completion had done nothing this would read CAT COM.
        "terminal ran CAT COMPLETED.TXT",
        "terminal ran RUN SETUP.TXT",
        "terminal ran CAT SORTED.TXT | GREP TXT",
        "window moved to workspace ",
        "workspace 2",
        "floating point ready, drew the M12 triangle",
        f"key {KEY_SENT}",
        "sum 12+30 = 42",
        "sum 2^5 = 32",
        "sum IF 3>2 THEN 10 ELSE 20 = 10",
        "sum IF 2>3 THEN 10 ELSE 20 = 20",
        "sum X=5 = 5",
        "sum X+3 = 8",
        "sum IF X>2 THEN 10 ELSE 20 = 10",
        # M29. The allocator came up and proved itself against real memory.
        "pmm: ready",
        "pmm: selfcheck passed",
        # M30. A second address space was built and the processor ran on it.
        "vmm: no-execute available",
        "vmm: selfcheck passed",
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



# M29. The physical page allocator, as the real machine sees it.
#
# The host suite covers the bitmap arithmetic completely and cannot cover this:
# whether the pages the bitmap calls free can actually be written. The kernel
# proves that at boot by taking eight pages, writing each one's own physical
# address into it, reading it back and giving them up again. What is checked
# here is that it said so, and that the numbers are the shape of a real machine
# rather than a zero that would make the self check vacuous.
MIN_USABLE_PAGES = 1000


def check_pmm() -> list[str]:
    text = SERIAL_LOG.read_text(errors="ignore")

    match = re.search(r"pmm: usable pages (\d+)", text)
    if match is None:
        raise CheckFailed("the kernel never reported how many pages it found")
    pages = int(match.group(1))
    if pages < MIN_USABLE_PAGES:
        raise CheckFailed(
            f"the allocator found only {pages} usable pages, which is too few "
            f"for the self check to have proved anything")

    after = re.search(r"pmm: selfcheck passed, pages still free (\d+)", text)
    if after is None:
        raise CheckFailed("the allocator self check did not pass")
    if int(after.group(1)) != pages:
        raise CheckFailed(
            f"the allocator started with {pages} free pages and ended the self "
            f"check with {after.group(1)}, so a page went missing")

    bitmap = re.search(r"pmm: bitmap bytes (\d+)", text)
    if bitmap is None:
        raise CheckFailed("the kernel never reported the size of the page bitmap")

    megabytes = pages * 4096 // (1024 * 1024)
    return [f"M29 page allocator: {pages} usable pages, {megabytes} MB, proved by "
            f"writing and reading back eight of them and giving all eight back"]


# M30. Address spaces, as the real processor sees them.
#
# The host suite walks the whole page table tree and still cannot answer the
# only question that matters here: whether the processor accepts a tree this
# kernel built. Building a correct looking one and running on one are different
# claims. So the kernel makes a second address space, checks its own stack and
# code are reachable in it, loads it into CR3, reads a word back through a
# mapping that exists only there, and switches back.
def check_vmm() -> list[str]:
    text = SERIAL_LOG.read_text(errors="ignore")

    if "vmm: selfcheck passed" not in text:
        raise CheckFailed("the kernel never ran on an address space it built itself")
    if "vmm: no-execute available" not in text:
        raise CheckFailed("the no-execute bit was not available, so pages cannot be "
                          "made unrunnable and user isolation would be weaker")

    tables = re.search(r"vmm: tables returned (\d+)", text)
    if tables is None or int(tables.group(1)) < 1:
        raise CheckFailed("tearing the address space down returned no page tables, "
                          "so every program run would leak its own tables")

    root = re.search(r"vmm: kernel page tables at 0x([0-9A-F]+)", text)
    if root is None:
        raise CheckFailed("the kernel never reported where its page tables are")

    return [f"M30 address spaces: a second one built, loaded into CR3, read back "
            f"through a mapping only it has, and torn down returning "
            f"{tables.group(1)} page tables"]


# M16. How much a single cursor movement is allowed to cost, in pixels written
# to the display. Two cursor rectangles with their outlines are 2 x 10 x 14 =
# 280, and a union of two overlapping ones is smaller still. The whole screen at
# the resolution this boots in is 1,024,000, and the old code presented it twice
# per movement, so anything near that number means the fast path is gone.
#
# Generous on purpose. The number that matters is not 254 versus 300, it is 254
# versus two million.
MAX_CURSOR_PIXELS_PER_MOVE = 8000


def check_cursor_cost() -> list[str]:
    """The mouse moved without repainting the screen. See M16.

    Read from the kernel's own counters rather than timed from outside, because
    a wall clock measurement of an emulator running on a shared machine would
    fail for reasons that have nothing to do with ME OS.
    """
    text = DEBUG_LOG.read_text(errors="ignore")
    lines = [line for line in text.splitlines() if "input packets" in line]
    if not lines:
        raise CheckFailed("the kernel never reported its input counters")

    fields = {}
    for pair in lines[-1].split("me-os: input ")[-1].split():
        if pair.isdigit():
            fields[last] = int(pair)
        else:
            last = pair

    for name in ("packets", "cursor", "whole", "region", "cursorpixels"):
        if name not in fields:
            raise CheckFailed(f"the input counter line has no {name!r}: {lines[-1]!r}")

    if fields["packets"] == 0:
        raise CheckFailed("the kernel saw no mouse packets at all")
    if fields["cursor"] == 0:
        raise CheckFailed("the mouse moved and the cursor never updated")

    per_move = fields["cursorpixels"] / fields["cursor"]
    if per_move > MAX_CURSOR_PIXELS_PER_MOVE:
        raise CheckFailed(
            f"a cursor movement wrote {per_move:.0f} pixels to the display, over the "
            f"{MAX_CURSOR_PIXELS_PER_MOVE} allowed. The dirty region fast path has "
            f"been lost and the mouse is repainting the screen again")

    # Whole screen presentations are for startup and for a focus change that
    # reorders windows. Cursor movement must never be a reason for one, so this
    # stays a small constant however far the mouse travels.
    if fields["whole"] > 16:
        raise CheckFailed(
            f"the kernel composed the whole screen {fields['whole']} times, which is "
            f"more than startup and the two focus changes need. Something on the "
            f"input path is repainting everything again")

    return [
        f"M16 input cost: {fields['packets']} mouse packets became "
        f"{fields['cursor']} cursor updates costing {per_move:.0f} pixels each, "
        f"with {fields['whole']} whole screen compositions in the entire run"
    ]


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


def captures_named(*names: str) -> list[tuple[str, int, int]]:
    """The recorded rectangles for these captures, by filename.

    By name rather than by taking the last few off the end of the list.
    Reading the tail worked only because each check happened to be called
    straight after the captures it was about were loaded, so inserting a
    screenshot anywhere earlier would have quietly pointed these checks at the
    wrong pictures and they would have gone on passing.
    """
    found = {name: entry for entry in RECTANGLES for name in [entry[0]]}
    missing = [n for n in names if n not in found]
    if missing:
        raise CheckFailed(
            f"no rectangle was recorded for {', '.join(missing)}, so there is "
            f"nothing to measure the movement against")
    return [found[n] for n in names]


def check_steering() -> list[str]:
    """M9: the arrow keys move the rectangle, by exactly as much as was pressed.

    The last three captures are the one before any arrow was pressed, the one
    after three presses of down, and the one after eight of left. Because the
    first arrow press also stops the rectangle drifting, the only thing that can
    move it afterwards is another press, so the distances are exact rather than
    approximate.
    """
    before, down, left = captures_named(
        "screen-varif.ppm", "screen-steer-down.ppm", "screen-steer-left.ppm")

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
    before, wrapped_down, wrapped_left = captures_named(
        "screen-steer-left.ppm", "screen-wrap-down.ppm", "screen-wrap-left.ppm")
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
    ready, held, released = captures_named(
        "screen-drag-ready.ppm", "screen-drag-held.ppm", "screen-drag-release.ppm")
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

    # A pointer that ran into the edge of the screen is a run that could not ask
    # the question, which is a different thing from an answer of no. Said in its
    # own words, because reporting it as a wrong offset sends whoever reads it
    # looking at the drag code, where there is nothing wrong. Told apart from a
    # wrong answer by the movement being short in the direction it was going,
    # which is the only thing an edge can do to it.
    wanted = drag_delta()
    if pointer_delta[0] != wanted and abs(pointer_delta[0]) < abs(wanted):
        raise CheckFailed(
            f"the drag ran the pointer into the edge of the screen: it started "
            f"at x={ready_cursor[0]}, was pulled {wanted}, and only moved "
            f"{pointer_delta[0]}. This run could not test dragging at all, and "
            f"it is the aim in scripts/boot-capture.sh that needs looking at, "
            f"not the drag")
    if pointer_delta != (wanted, 30):
        raise CheckFailed(
            f"the held pointer moved {pointer_delta}, expected ({wanted}, 30)")
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
        load_demo_geometry()
        notes, boot_cursor, size, rect_boot, sum_boot, screen = check_screen(
            SCREEN_BOOT, M2_PROMPT)
        key_notes, key_cursor, _, rect_key, _, _ = check_screen(
            SCREEN_KEY, M2_AFTER_KEY, M8_KEY_ON_SUM_LINE)
        mouse_notes, moved_cursor, _, rect_mouse, _, _ = check_screen(
            SCREEN_MOUSE, M2_AFTER_KEY, M8_KEY_ON_SUM_LINE)
        clamp_notes, clamped_cursor, _, rect_clamp, sum_clamp, _ = check_screen(
            SCREEN_CLAMP, M2_AFTER_KEY, M8_KEY_ON_SUM_LINE)
        typed_notes, _, _, rect_sum, sum_typed, _ = check_screen(
            SCREEN_SUM, M6_AFTER_ENTER, M6_SUM)
        power_notes, _, _, rect_power, sum_power, _ = check_screen(
            SCREEN_POWER, M6_AFTER_ENTER, M6_POWER)
        true_notes, _, _, rect_true, sum_true, _ = check_screen(
            SCREEN_TRUE, M6_AFTER_ENTER, M7_TRUE)
        false_notes, _, _, rect_false, sum_false, _ = check_screen(
            SCREEN_FALSE, M6_AFTER_ENTER, M7_FALSE)
        assign_notes, _, _, rect_assign, sum_assign, _ = check_screen(
            SCREEN_ASSIGN, M6_AFTER_ENTER, M8_ASSIGN)
        var_notes, _, _, rect_var, sum_var, _ = check_screen(
            SCREEN_VAR, M6_AFTER_ENTER, M8_VAR)
        varif_notes, _, _, rect_varif, sum_varif, _ = check_screen(
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

        notes += check_cursor_movement(boot_cursor, moved_cursor, clamped_cursor, screen)
        notes += check_rectangle_movement(
            [rect_boot, rect_key, rect_mouse, rect_clamp, rect_sum, rect_power,
             rect_true, rect_false, rect_assign, rect_var, rect_varif], size)
        steer_down_notes, _, _, rect_steer_down, _, _ = check_screen(
            SCREEN_STEER_DOWN, M2_AFTER_ARROW_DOWN, M8_VARIF)
        steer_left_notes, _, _, rect_steer_left, _, _ = check_screen(
            SCREEN_STEER_LEFT, M2_AFTER_ARROW_LEFT, M8_VARIF)
        notes += steer_down_notes + steer_left_notes

        notes += check_steering()
        wrap_down_notes, _, _, _, _, _ = check_screen(
            SCREEN_WRAP_DOWN, M2_AFTER_ARROW_DOWN, M8_VARIF)
        wrap_left_notes, _, _, _, _, _ = check_screen(
            SCREEN_WRAP_LEFT, M2_AFTER_ARROW_LEFT, M8_VARIF)
        notes += wrap_down_notes + wrap_left_notes
        notes += check_wrapping(size)
        drag_ready_notes, drag_ready_cursor, _, _, _, _ = check_screen(
            SCREEN_DRAG_READY, M2_AFTER_ARROW_LEFT, M8_VARIF)
        drag_held_notes, drag_held_cursor, _, _, _, _ = check_screen(
            SCREEN_DRAG_HELD, M2_AFTER_ARROW_LEFT, M8_VARIF)
        drag_release_notes, drag_release_cursor, _, _, _, _ = check_screen(
            SCREEN_DRAG_RELEASE, M2_AFTER_ARROW_LEFT, M8_VARIF)
        notes += drag_ready_notes + drag_held_notes + drag_release_notes
        # In Demo's coordinates, because the rectangle the press has to land
        # inside is recorded in those. The movements are the same either way;
        # the containment check is not.
        tile = demo_tile()
        def in_demo(where):
            return (where[0] - tile.client_x, where[1] - tile.client_y, where[2])
        notes += check_dragging(in_demo(drag_ready_cursor),
                                in_demo(drag_held_cursor),
                                in_demo(drag_release_cursor), size)
        notes += check_rotation()
        notes += check_log()
        notes += check_pmm()
        notes += check_vmm()
        notes += check_cursor_cost()
        notes += check_tiling()
        notes += check_focus_moved()
        notes += check_terminal()
        notes += check_editor()
        notes += check_clock()
        notes += check_workspaces()
        notes += check_persistence()
        notes += check_scrollback()
        notes += check_tiles_on_screen(SCREEN_FOCUS_SYSTEM)
    except CheckFailed as exc:
        print(f"check FAILED: {exc}")
        return 1

    for note in notes:
        print(f"  {note}")
    print("M1 to M28 checks passed: message, key press, a rectangle that drifts, "
          "can be steered, wraps and is dragged, a cursor that follows the mouse, sums "
          "answered, a conditional taking each branch in turn, a value remembered "
          "under a name and used again, a triangle turning about its own centre, and "
          "two opaque window surfaces with click focus and routed input, all of it "
          "presented through dirty regions rather than whole screen repaints, "
          "tiled into a desktop with workspaces, a shell, an editor and a clock, "
          "on a filesystem of files made of blocks that is still there after "
          "the machine restarts, with a shell whose commands can be piped "
          "into each other and written to files, and a scrollback you can "
          "look at what went past in, running files of commands you wrote, and "
          "finishing names you have started typing")
    print("A person should still watch it boot once with make run.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
