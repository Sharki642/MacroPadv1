#!/usr/bin/env python3
"""MacroPad Manager: change what the 16 keys do on each of the 4 tabs over USB.

The pad stores everything in its own flash, so this app only needs to run
while you change something. Needs PySide6 (no other dependencies).
"""
import argparse
import glob
import json
import os
import select
import sys
import termios
import time

from PySide6.QtCore import QRegularExpression, QSettings, QSize, Qt, QTimer
from PySide6.QtGui import QIcon, QImage, QPixmap, QRegularExpressionValidator
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QFileDialog, QFormLayout, QGridLayout,
    QGroupBox, QHBoxLayout, QLabel, QLineEdit, QMainWindow, QMessageBox,
    QPushButton, QStackedWidget, QTabBar, QToolButton, QVBoxLayout,
    QWidget,
)

NUM_TABS = 4
NUM_KEYS = 16
LABEL_LEN = 5
MAX_TEXT = 48
USB_VENDOR = "239a"

ACT_NONE, ACT_KEY, ACT_TEXT, ACT_RUN, ACT_MEDIA, ACT_MOUSE = range(6)
ACTION_NAMES = ["Nothing", "Keyboard shortcut", "Type text", "Run program",
                "Media / system key", "Mouse button"]

M_CTRL, M_SHIFT, M_ALT, M_SUPER, M_ALTGR = 0x01, 0x02, 0x04, 0x08, 0x40
MOD_NAMES = [(M_CTRL, "Ctrl"), (M_SHIFT, "Shift"), (M_ALT, "Alt"),
             (M_SUPER, "Meta"), (M_ALTGR, "AltGr")]

MEDIA_KEYS = [
    (0x00CD, "Play / pause"), (0x00B5, "Next track"), (0x00B6, "Previous track"),
    (0x00B7, "Stop"), (0x00E2, "Mute"), (0x00E9, "Volume up"),
    (0x00EA, "Volume down"), (0x006F, "Brightness up"),
    (0x0070, "Brightness down"), (0x0192, "Calculator"),
    (0x0223, "Browser home"), (0x0224, "Browser back"),
    (0x0225, "Browser forward"), (0x0227, "Browser refresh"), (0x0082, "Sleep"),
]
MOUSE_BUTTONS = [(1, "Left"), (2, "Right"), (4, "Middle"), (8, "Back"),
                 (16, "Forward")]

ICON_NAMES = ["Globe", "Folder", "Camera", "Lock", "Arrow left", "Arrow right",
              "Papers", "Clipboard", "Floppy", "Check", "Magnifier", "Cross",
              "Plus", "Refresh", "Power", "Monitor"]
ICONS = [bytes.fromhex(h) for h in """
000007e018182004400243c2842188118811842143c240022004181807e00000
000000003f8020403ffc20042004200420042004200420043ffc000000000000
000000000ff010083ffc210425a425a421043ffc20043ffc0000000000000000
0000000007e00810081008101ff81bb81bb81bb81bb81bb81ff8000000000000
000000000180030007000fff0f80070003000180000000000000000000000000
0000000018000c000e00fff001f000e000c00180000000000000000000000000
000000001fe010201fe01020ffe080208020802080208020ffe0000000000000
0000000007e01c383ffc2004200420043ffc2004200420043ffc000000000000
00000000ffff80018ff1810180018001800180018ff18101ffff000000000000
0000000000020006000c001800304060e0c071803b001e000c00000000000000
0000000007c00820101010101010082007c00040008000000100020000000000
00000000318c1bd80ff006600ff01bd8318c0000000000000000000000000000
000000000180018001800180ffffffff01800180018001800000000000000000
000000000fe03018400480028002860288028002400430180fe0000000000000
0000000007e01818200440024002400240022004181807e00180018001800000
000000007ffe4002400240024002400240027ffe018001800ff0000000000000
""".split()]


# Physical keys: (HID usage, US name, German name)
def build_keys():
    keys = []
    for i, ch in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ"):
        de = {"Y": "Z", "Z": "Y"}.get(ch, ch)
        keys.append((0x04 + i, ch, de))
    for i, ch in enumerate("1234567890"):
        keys.append((0x1E + i, ch, ch))
    keys += [
        (0x28, "Enter", "Enter"), (0x29, "Esc", "Esc"),
        (0x2A, "Backspace", "Backspace"), (0x2B, "Tab", "Tab"),
        (0x2C, "Space", "Space"), (0x2D, "-", "ß"), (0x2E, "=", "´"),
        (0x2F, "[", "Ü"), (0x30, "]", "+"), (0x31, "\\", "#"),
        (0x33, ";", "Ö"), (0x34, "'", "Ä"), (0x35, "`", "^"), (0x36, ",", ","),
        (0x37, ".", "."), (0x38, "/", "-"), (0x64, "ISO \\", "<"),
        (0x39, "Caps Lock", "Caps Lock"),
    ]
    keys += [(0x3A + i, f"F{i + 1}", f"F{i + 1}") for i in range(12)]
    keys += [(0x68 + i, f"F{i + 13}", f"F{i + 13}") for i in range(12)]
    keys += [
        (0x46, "Print", "Print"), (0x47, "Scroll Lock", "Scroll Lock"),
        (0x48, "Pause", "Pause"), (0x49, "Insert", "Insert"),
        (0x4A, "Home", "Home"), (0x4B, "Page Up", "Page Up"),
        (0x4C, "Delete", "Delete"), (0x4D, "End", "End"),
        (0x4E, "Page Down", "Page Down"), (0x4F, "Right", "Right"),
        (0x50, "Left", "Left"), (0x51, "Down", "Down"), (0x52, "Up", "Up"),
        (0x53, "Num Lock", "Num Lock"), (0x54, "Num /", "Num /"),
        (0x55, "Num *", "Num *"), (0x56, "Num -", "Num -"),
        (0x57, "Num +", "Num +"), (0x58, "Num Enter", "Num Enter"),
    ]
    keys += [(0x59 + i, f"Num {(i + 1) % 10}", f"Num {(i + 1) % 10}") for i in range(10)]
    keys += [(0x63, "Num .", "Num ,")]
    return keys


KEYS = build_keys()

# Linux evdev code -> HID usage, for recording what you press
EVDEV_TO_HID = {1: 0x29, 14: 0x2A, 15: 0x2B, 28: 0x28, 57: 0x2C, 12: 0x2D,
                13: 0x2E, 26: 0x2F, 27: 0x30, 43: 0x31, 39: 0x33, 40: 0x34,
                41: 0x35, 51: 0x36, 52: 0x37, 53: 0x38, 86: 0x64, 58: 0x39,
                87: 0x44, 88: 0x45, 99: 0x46, 70: 0x47, 119: 0x48, 110: 0x49,
                102: 0x4A, 104: 0x4B, 111: 0x4C, 107: 0x4D, 109: 0x4E,
                106: 0x4F, 105: 0x50, 108: 0x51, 103: 0x52, 69: 0x53, 98: 0x54,
                55: 0x55, 74: 0x56, 78: 0x57, 96: 0x58, 71: 0x5F, 72: 0x60,
                73: 0x61, 75: 0x5C, 76: 0x5D, 77: 0x5E, 79: 0x59, 80: 0x5A,
                81: 0x5B, 82: 0x62, 83: 0x63}
EVDEV_TO_HID.update({2 + i: 0x1E + i for i in range(10)})
EVDEV_TO_HID.update({59 + i: 0x3A + i for i in range(10)})
for row, first in (("qwertyuiop", 16), ("asdfghjkl", 30), ("zxcvbnm", 44)):
    for i, ch in enumerate(row):
        EVDEV_TO_HID[first + i] = 0x04 + ord(ch) - ord("a")
EVDEV_MODS = {29: M_CTRL, 97: M_CTRL, 42: M_SHIFT, 54: M_SHIFT, 56: M_ALT,
              100: M_ALTGR, 125: M_SUPER, 126: M_SUPER}


def add_row(table, chars, usages, mods=0):
    for ch, usage in zip(chars, usages):
        if ch != " ":
            table[ch] = (mods, usage)


NUMBER_ROW = [0x1E + i for i in range(10)]


def build_common():
    table = {" ": (0, 0x2C), "\n": (0, 0x28), "\t": (0, 0x2B)}
    add_row(table, "1234567890", NUMBER_ROW)
    return table


def build_de():
    table = build_common()
    for i, ch in enumerate("abcdefghijklmnopqrstuvwxyz"):
        usage = 0x04 + i
        if ch in "yz":
            usage = 0x1D if ch == "y" else 0x1C
        table[ch] = (0, usage)
        table[ch.upper()] = (M_SHIFT, usage)
    add_row(table, '!"§$%&/()=', NUMBER_ROW, M_SHIFT)
    add_row(table, "      {[]}", NUMBER_ROW, M_ALTGR)
    add_row(table, "ßüö ä#,.-<+", [0x2D, 0x2F, 0x33, 0, 0x34, 0x31, 0x36, 0x37, 0x38, 0x64, 0x30])
    add_row(table, "?ÜÖ Ä';:_>*", [0x2D, 0x2F, 0x33, 0, 0x34, 0x31, 0x36, 0x37, 0x38, 0x64, 0x30], M_SHIFT)
    add_row(table, "\\@€|~", [0x2D, 0x14, 0x08, 0x64, 0x30], M_ALTGR)
    return table


def build_us():
    table = build_common()
    for i, ch in enumerate("abcdefghijklmnopqrstuvwxyz"):
        table[ch] = (0, 0x04 + i)
        table[ch.upper()] = (M_SHIFT, 0x04 + i)
    add_row(table, "!@#$%^&*()", NUMBER_ROW, M_SHIFT)
    usages = [0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38]
    add_row(table, "-=[]\\;'`,./", usages)
    add_row(table, '_+{}|:"~<>?', usages, M_SHIFT)
    return table


LAYOUTS = {"German / Austrian (QWERTZ)": ("de", build_de()),
           "US (QWERTY)": ("us", build_us())}


def text_to_steps(text, table):
    """Returns (steps, unsupported characters)."""
    steps, bad = [], []
    for ch in text:
        if ch in table:
            steps.append(table[ch])
        else:
            bad.append(ch)
    return steps, bad


def steps_to_text(steps, table):
    reverse = {v: k for k, v in table.items()}
    return "".join(reverse.get(tuple(s), "?") for s in steps)


def key_name(usage, lang):
    for code, us, de in KEYS:
        if code == usage:
            return de if lang == "de" else us
    return f"0x{usage:02X}"


def describe(cfg, lang, table):
    kind = cfg["type"]
    if kind == ACT_KEY:
        parts = [name for bit, name in MOD_NAMES if cfg["mods"] & bit]
        if cfg["code"]:
            parts.append(key_name(cfg["code"], lang))
        return "+".join(parts) or "(no key)"
    if kind == ACT_TEXT:
        return '"' + steps_to_text(cfg["steps"], table) + '"'
    if kind == ACT_RUN:
        return "run " + steps_to_text(cfg["steps"], table)
    if kind == ACT_MEDIA:
        return dict(MEDIA_KEYS).get(cfg["code"], f"code {cfg['code']:#x}")
    if kind == ACT_MOUSE:
        return dict(MOUSE_BUTTONS).get(cfg["code"], "?") + " click"
    return "-"


class PadError(Exception):
    pass


class PadLink:
    """Line protocol to the pad over its USB serial port."""

    def __init__(self, path):
        self.path = path
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.buf = b""
        try:
            attrs = termios.tcgetattr(self.fd)
            attrs[0] = 0
            attrs[1] = 0
            attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            attrs[3] = 0
            attrs[4] = attrs[5] = termios.B115200
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
            termios.tcflush(self.fd, termios.TCIOFLUSH)
            reply = self.command("HELLO", lambda line: line.startswith("MACROPAD"))
            if reply[-1].split()[-1] != "1":
                raise PadError("This firmware speaks a different protocol version.")
        except (OSError, termios.error, PadError):
            self.close()
            raise

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def alive(self):
        return self.fd is not None and os.path.exists(self.path)

    def _read_line(self, deadline):
        while b"\n" not in self.buf:
            left = deadline - time.monotonic()
            if left <= 0:
                raise PadError("The pad did not answer.")
            ready, _, _ = select.select([self.fd], [], [], left)
            if ready:
                data = os.read(self.fd, 4096)
                if not data:
                    raise PadError("The pad went away.")
                self.buf += data
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("ascii", "replace").strip()

    def command(self, line, is_last, timeout=2.0):
        try:
            os.write(self.fd, (line + "\n").encode("ascii"))
            deadline = time.monotonic() + timeout
            lines = []
            while True:
                reply = self._read_line(deadline)
                if reply:
                    lines.append(reply)
                    if is_last(reply):
                        return lines
        except OSError as err:
            raise PadError(f"USB error: {err}") from err

    def ok(self, line):
        reply = self.command(line, lambda r: r in ("OK", "ERR"))
        if reply[-1] != "OK":
            raise PadError("The pad rejected the change (flash full?).")

    def read_all(self):
        tabs = [""] * NUM_TABS
        keys = [[None] * NUM_KEYS for _ in range(NUM_TABS)]
        for line in self.command("GET", lambda r: r == "END", timeout=5.0):
            f = line.split()
            if f[0] == "TAB" and len(f) == 3:
                tabs[int(f[1])] = f[2]
            elif f[0] == "KEY" and len(f) == 10:
                raw = b"" if f[9] == "-" else bytes.fromhex(f[9])
                keys[int(f[1])][int(f[2])] = {
                    "type": int(f[3]), "mods": int(f[4]), "code": int(f[5]),
                    "icon": int(f[6]), "hold": int(f[7]),
                    "label": "" if f[8] == "-" else bytes.fromhex(f[8]).decode("ascii", "replace"),
                    "steps": [[raw[i], raw[i + 1]] for i in range(0, len(raw), 2)],
                }
        if not all(tabs) or any(k is None for tab in keys for k in tab):
            raise PadError("Incomplete answer from the pad.")
        return tabs, keys

    def write_key(self, tab, key, cfg):
        label = cfg["label"].encode("ascii").hex() or "-"
        text = bytes(b for step in cfg["steps"] for b in step).hex() or "-"
        self.ok(f"SET {tab} {key} {cfg['type']} {cfg['mods']} {cfg['code']} "
                f"{cfg['icon']} {cfg['hold']} {label} {text}")

    def write_name(self, tab, name):
        self.ok(f"NAME {tab} {name}")

    def reset(self):
        self.ok("RESET")


def find_port():
    for tty in sorted(glob.glob("/sys/class/tty/ttyACM*")):
        device = os.path.realpath(os.path.join(tty, "device"))
        try:
            with open(os.path.join(device, "..", "idVendor")) as f:
                if f.read().strip() == USB_VENDOR:
                    return "/dev/" + os.path.basename(tty)
        except OSError:
            continue
    return None


def icon_pixmap(index, color=0x35C4F0, scale=2):
    image = QImage(16, 16, QImage.Format_ARGB32)
    image.fill(0)
    data = ICONS[index]
    for y in range(16):
        row = (data[y * 2] << 8) | data[y * 2 + 1]
        for x in range(16):
            if row & (0x8000 >> x):
                image.setPixel(x, y, 0xFF000000 | color)
    return QPixmap.fromImage(image).scaled(16 * scale, 16 * scale)


class RecordButton(QPushButton):
    """Click, then press a key combination. Uses physical keys."""

    IDLE = "Record"

    def __init__(self, on_record):
        super().__init__(self.IDLE)
        self.on_record = on_record
        self.recording = False
        self.mods = 0
        self.setCheckable(True)
        self.toggled.connect(self._toggled)

    def _toggled(self, active):
        self.recording = active
        self.mods = 0
        self.setText("Press keys..." if active else self.IDLE)
        if active:
            self.setFocus()
            self.grabKeyboard()
        else:
            self.releaseKeyboard()

    def _stop(self):
        self.setChecked(False)

    def focusOutEvent(self, event):
        self._stop()
        super().focusOutEvent(event)

    def event(self, event):
        # Let Tab reach the recorder instead of moving the focus
        if self.recording and event.type() in (event.Type.KeyPress, event.Type.KeyRelease):
            self._key(event, event.type() == event.Type.KeyPress)
            return True
        return super().event(event)

    def _key(self, event, pressed):
        if event.isAutoRepeat():
            return
        evdev = event.nativeScanCode() - 8
        if evdev in EVDEV_MODS:
            if pressed:
                self.mods |= EVDEV_MODS[evdev]
            elif self.mods:
                self._finish(self.mods, 0)     # modifier on its own
        elif pressed and evdev in EVDEV_TO_HID:
            self._finish(self.mods, EVDEV_TO_HID[evdev])

    def _finish(self, mods, code):
        self._stop()
        self.on_record(mods, code)


class MainWindow(QMainWindow):
    def __init__(self, port_override):
        super().__init__()
        self.port_override = port_override
        self.link = None
        self.tabs = ["GEN", "EDA", "CHL", "GAM"]
        self.keys = None
        self.tab = 0
        self.key = 0
        self.loading = False
        self.dirty = None
        self.settings = QSettings("MacroPad", "Manager")

        self.setWindowTitle("MacroPad Manager")
        self._build_ui()

        self.save_timer = QTimer(self, singleShot=True, interval=500)
        self.save_timer.timeout.connect(self.flush)
        self.watch = QTimer(self, interval=1500)
        self.watch.timeout.connect(self.check_connection)
        self.watch.start()
        self.check_connection()

    # ---- layout helpers ----
    @property
    def lang(self):
        return LAYOUTS[self.layout_box.currentText()][0]

    @property
    def table(self):
        return LAYOUTS[self.layout_box.currentText()][1]

    @property
    def cfg(self):
        return self.keys[self.tab][self.key]

    # ---- UI ----
    def _build_ui(self):
        root = QWidget()
        self.setCentralWidget(root)
        outer = QVBoxLayout(root)

        top = QHBoxLayout()
        self.status = QLabel()
        self.layout_box = QComboBox()
        self.layout_box.addItems(LAYOUTS)
        saved = self.settings.value("layout", "")
        if saved in LAYOUTS:
            self.layout_box.setCurrentText(saved)
        self.layout_box.currentTextChanged.connect(self.layout_changed)
        top.addWidget(self.status, 1)
        top.addWidget(QLabel("PC keyboard layout:"))
        top.addWidget(self.layout_box)
        outer.addLayout(top)

        tab_row = QHBoxLayout()
        self.tab_bar = QTabBar()
        self.tab_bar.setExpanding(False)
        for name in self.tabs:
            self.tab_bar.addTab(name)
        self.tab_bar.currentChanged.connect(self.select_tab)
        self.tab_name = QLineEdit()
        self.tab_name.setMaxLength(3)
        self.tab_name.setFixedWidth(60)
        self.tab_name.setValidator(QRegularExpressionValidator(
            QRegularExpression(r"[!-~]{1,3}")))
        self.tab_name.editingFinished.connect(self.rename_tab)
        tab_row.addWidget(self.tab_bar)
        tab_row.addStretch(1)
        tab_row.addWidget(QLabel("Tab name:"))
        tab_row.addWidget(self.tab_name)
        outer.addLayout(tab_row)

        body = QHBoxLayout()
        grid = QGridLayout()
        grid.setSpacing(6)
        self.buttons = []
        for i in range(NUM_KEYS):
            button = QToolButton()
            button.setCheckable(True)
            button.setAutoExclusive(True)
            button.setFixedSize(QSize(118, 92))
            button.setIconSize(QSize(32, 32))
            button.setToolButtonStyle(Qt.ToolButtonTextUnderIcon)
            button.clicked.connect(lambda _=False, k=i: self.select_key(k))
            grid.addWidget(button, i // 4, i % 4)
            self.buttons.append(button)
        body.addLayout(grid)
        body.addWidget(self._build_editor(), 1)
        outer.addLayout(body)

        bottom = QHBoxLayout()
        export_button = QPushButton("Export...")
        import_button = QPushButton("Import...")
        reset_button = QPushButton("Reset to defaults...")
        export_button.clicked.connect(self.export_config)
        import_button.clicked.connect(self.import_config)
        reset_button.clicked.connect(self.reset_config)
        self.saved_label = QLabel()
        bottom.addWidget(self.saved_label, 1)
        bottom.addWidget(export_button)
        bottom.addWidget(import_button)
        bottom.addWidget(reset_button)
        outer.addLayout(bottom)
        self.tools = [export_button, import_button, reset_button]

    def _build_editor(self):
        box = QGroupBox("Key")
        self.editor = box
        form = QFormLayout(box)

        self.label_edit = QLineEdit()
        self.label_edit.setMaxLength(LABEL_LEN)
        self.label_edit.setValidator(QRegularExpressionValidator(
            QRegularExpression(r"[ -~]*")))
        self.label_edit.textEdited.connect(self.changed)
        form.addRow("Label on display:", self.label_edit)

        self.icon_box = QComboBox()
        self.icon_box.setIconSize(QSize(16, 16))
        self.icon_box.addItem("None (show label)", -1)
        for i, name in enumerate(ICON_NAMES):
            self.icon_box.addItem(QIcon(icon_pixmap(i, scale=1)), name, i)
        self.icon_box.activated.connect(self.changed)
        form.addRow("Icon:", self.icon_box)

        self.type_box = QComboBox()
        self.type_box.addItems(ACTION_NAMES)
        self.type_box.activated.connect(self.type_changed)
        form.addRow("Action:", self.type_box)

        self.pages = QStackedWidget()
        self.pages.addWidget(QWidget())
        self.pages.addWidget(self._key_page())
        self.text_edit, text_page = self._text_page(
            "Typed exactly as written, using the layout chosen at the top.")
        self.run_edit, run_page = self._text_page(
            "Opens KRunner (Alt+F2), types this and presses Enter.")
        self.pages.addWidget(text_page)
        self.pages.addWidget(run_page)
        self.media_box = QComboBox()
        for code, name in MEDIA_KEYS:
            self.media_box.addItem(name, code)
        self.media_box.activated.connect(self.changed)
        self.pages.addWidget(self.media_box)
        self.pages.addWidget(self._mouse_page())
        form.addRow(self.pages)

        self.error = QLabel()
        self.error.setStyleSheet("color: #e05252;")
        self.error.setWordWrap(True)
        form.addRow(self.error)
        return box

    def _key_page(self):
        page = QWidget()
        col = QVBoxLayout(page)
        col.setContentsMargins(0, 0, 0, 0)
        mods = QHBoxLayout()
        self.mod_boxes = {}
        for bit, name in MOD_NAMES:
            check = QCheckBox(name)
            check.clicked.connect(self.changed)
            self.mod_boxes[bit] = check
            mods.addWidget(check)
        mods.addStretch(1)
        col.addLayout(mods)
        row = QHBoxLayout()
        self.key_box = QComboBox()
        self.key_box.setEditable(False)
        self.fill_key_box()
        self.key_box.activated.connect(self.changed)
        self.record = RecordButton(self.recorded)
        row.addWidget(self.key_box, 1)
        row.addWidget(self.record)
        col.addLayout(row)
        self.hold_check = QCheckBox("Hold while pressed (for games, push to talk)")
        self.hold_check.clicked.connect(self.changed)
        col.addWidget(self.hold_check)
        col.addStretch(1)
        return page

    def _mouse_page(self):
        page = QWidget()
        col = QVBoxLayout(page)
        col.setContentsMargins(0, 0, 0, 0)
        self.mouse_box = QComboBox()
        for code, name in MOUSE_BUTTONS:
            self.mouse_box.addItem(name, code)
        self.mouse_box.activated.connect(self.changed)
        self.mouse_hold = QCheckBox("Hold while pressed (for dragging)")
        self.mouse_hold.clicked.connect(self.changed)
        col.addWidget(self.mouse_box)
        col.addWidget(self.mouse_hold)
        col.addStretch(1)
        return page

    def _text_page(self, hint):
        page = QWidget()
        col = QVBoxLayout(page)
        col.setContentsMargins(0, 0, 0, 0)
        edit = QLineEdit()
        edit.textEdited.connect(self.changed)
        note = QLabel(hint)
        note.setWordWrap(True)
        col.addWidget(edit)
        col.addWidget(note)
        col.addStretch(1)
        return edit, page

    def fill_key_box(self):
        current = self.key_box.currentData()
        self.key_box.clear()
        self.key_box.addItem("(modifier only)", 0)
        lang = LAYOUTS[self.layout_box.currentText()][0]
        for code, us, de in KEYS:
            self.key_box.addItem(de if lang == "de" else us, code)
        index = self.key_box.findData(current)
        self.key_box.setCurrentIndex(max(index, 0))

    # ---- connection ----
    def check_connection(self):
        if self.link and not self.link.alive():
            self.disconnected("Macropad unplugged.")
        if self.link:
            return
        port = self.port_override or find_port()
        if not port:
            self.set_online(False, "Macropad not found. Plug it in (it needs the "
                                   "new firmware once).")
            return
        try:
            self.link = PadLink(port)
            self.tabs, self.keys = self.link.read_all()
        except PermissionError:
            self.link = None
            self.set_online(False, f"No permission for {port}. Your user must be "
                                   "in the uucp group.")
            return
        except (OSError, termios.error, PadError) as err:
            self.disconnected(str(err))
            return
        self.set_online(True, f"Connected on {port}")
        self.refresh()

    def disconnected(self, message):
        if self.link:
            self.link.close()
        self.link = None
        self.save_timer.stop()
        self.dirty = None
        self.set_online(False, message)

    def set_online(self, online, message):
        self.status.setText(("● " if online else "○ ") + message)
        self.status.setWordWrap(True)
        self.editor.setEnabled(online)
        self.tab_bar.setEnabled(online)
        self.tab_name.setEnabled(online)
        for button in self.buttons + self.tools:
            button.setEnabled(online)

    # ---- display ----
    def refresh(self):
        for t, name in enumerate(self.tabs):
            self.tab_bar.setTabText(t, name)
        self.tab_name.setText(self.tabs[self.tab])
        for i in range(NUM_KEYS):
            self.refresh_button(i)
        self.buttons[self.key].setChecked(True)
        self.load_editor()

    def refresh_button(self, i):
        cfg = self.keys[self.tab][i]
        button = self.buttons[i]
        summary = describe(cfg, self.lang, self.table)
        if len(summary) > 16:
            summary = summary[:15] + "…"
        button.setText(f"{cfg['label']}\n{summary}")
        button.setToolTip(describe(cfg, self.lang, self.table))
        if cfg["icon"] >= 0:
            button.setIcon(QIcon(icon_pixmap(cfg["icon"])))
        else:
            button.setIcon(QIcon())

    def load_editor(self):
        self.loading = True
        cfg = self.cfg
        self.editor.setTitle(f"Key {self.key + 1} (row {self.key // 4 + 1}, "
                             f"column {self.key % 4 + 1})")
        self.label_edit.setText(cfg["label"])
        self.icon_box.setCurrentIndex(self.icon_box.findData(cfg["icon"]))
        self.type_box.setCurrentIndex(cfg["type"])
        self.pages.setCurrentIndex(cfg["type"])
        for bit, check in self.mod_boxes.items():
            check.setChecked(bool(cfg["mods"] & bit))
        self.key_box.setCurrentIndex(max(self.key_box.findData(
            cfg["code"] if cfg["type"] == ACT_KEY else 0), 0))
        self.hold_check.setChecked(bool(cfg["hold"]) and cfg["type"] == ACT_KEY)
        self.mouse_hold.setChecked(bool(cfg["hold"]) and cfg["type"] == ACT_MOUSE)
        text = steps_to_text(cfg["steps"], self.table)
        self.text_edit.setText(text if cfg["type"] == ACT_TEXT else "")
        self.run_edit.setText(text if cfg["type"] == ACT_RUN else "")
        self.media_box.setCurrentIndex(max(self.media_box.findData(cfg["code"]), 0))
        self.mouse_box.setCurrentIndex(max(self.mouse_box.findData(cfg["code"]), 0))
        self.error.clear()
        self.loading = False

    # ---- editing ----
    def select_tab(self, tab):
        if tab < 0 or self.keys is None:
            return
        self.flush()
        self.tab = tab
        self.tab_name.setText(self.tabs[tab])
        for i in range(NUM_KEYS):
            self.refresh_button(i)
        self.load_editor()

    def select_key(self, key):
        self.flush()
        self.key = key
        self.load_editor()

    def type_changed(self):
        self.pages.setCurrentIndex(self.type_box.currentIndex())
        self.changed()

    def recorded(self, mods, code):
        for bit, check in self.mod_boxes.items():
            check.setChecked(bool(mods & bit))
        self.key_box.setCurrentIndex(max(self.key_box.findData(code), 0))
        self.changed()

    def read_form(self):
        kind = self.type_box.currentIndex()
        cfg = {"type": kind, "mods": 0, "code": 0, "hold": 0,
               "icon": self.icon_box.currentData(),
               "label": self.label_edit.text().rstrip(), "steps": []}
        error = ""
        if kind == ACT_KEY:
            cfg["mods"] = sum(b for b, c in self.mod_boxes.items() if c.isChecked())
            cfg["code"] = self.key_box.currentData()
            cfg["hold"] = int(self.hold_check.isChecked())
            if not cfg["mods"] and not cfg["code"]:
                error = "Pick a key."
        elif kind in (ACT_TEXT, ACT_RUN):
            edit = self.text_edit if kind == ACT_TEXT else self.run_edit
            steps, bad = text_to_steps(edit.text(), self.table)
            cfg["steps"] = [list(s) for s in steps]
            if bad:
                error = "Can't type: " + " ".join(sorted(set(bad)))
            elif len(steps) > MAX_TEXT:
                error = f"Too long: {len(steps)} of {MAX_TEXT} characters."
            elif not steps:
                error = "Enter some text."
        elif kind == ACT_MEDIA:
            cfg["code"] = self.media_box.currentData()
        elif kind == ACT_MOUSE:
            cfg["code"] = self.mouse_box.currentData()
            cfg["hold"] = int(self.mouse_hold.isChecked())
        return cfg, error

    def changed(self, *_):
        if self.loading or self.keys is None:
            return
        cfg, error = self.read_form()
        self.error.setText(error)
        if error:
            self.dirty = None
            self.save_timer.stop()
            self.saved_label.setText("Not saved")
            return
        self.keys[self.tab][self.key] = cfg
        self.refresh_button(self.key)
        self.dirty = (self.tab, self.key)
        self.saved_label.setText("Saving...")
        self.save_timer.start()

    def flush(self):
        self.save_timer.stop()
        if not self.dirty or not self.link:
            return
        tab, key = self.dirty
        self.dirty = None
        try:
            self.link.write_key(tab, key, self.keys[tab][key])
            self.saved_label.setText("Saved to the pad")
        except PadError as err:
            self.saved_label.setText("Not saved")
            self.disconnected(str(err))

    def rename_tab(self):
        name = self.tab_name.text()
        if not self.link or not name or name == self.tabs[self.tab]:
            return
        try:
            self.link.write_name(self.tab, name)
        except PadError as err:
            self.disconnected(str(err))
            return
        self.tabs[self.tab] = name
        self.tab_bar.setTabText(self.tab, name)
        self.saved_label.setText("Saved to the pad")

    def layout_changed(self, name):
        self.settings.setValue("layout", name)
        self.fill_key_box()
        if self.keys is not None:
            self.refresh()

    # ---- whole config ----
    def export_config(self):
        self.flush()
        path, _ = QFileDialog.getSaveFileName(self, "Export", "macropad.json",
                                              "JSON (*.json)")
        if path:
            with open(path, "w", encoding="utf-8") as f:
                json.dump({"tabs": self.tabs, "keys": self.keys}, f, indent=1)

    def import_config(self):
        self.flush()
        path, _ = QFileDialog.getOpenFileName(self, "Import", "", "JSON (*.json)")
        if not path:
            return
        try:
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
            tabs, keys = data["tabs"], data["keys"]
            if len(tabs) != NUM_TABS or any(len(t) != NUM_KEYS for t in keys):
                raise ValueError
            for t in range(NUM_TABS):
                self.link.write_name(t, tabs[t])
                for k in range(NUM_KEYS):
                    self.link.write_key(t, k, keys[t][k])
            self.tabs, self.keys = self.link.read_all()
        except (OSError, ValueError, KeyError, TypeError) as err:
            QMessageBox.warning(self, "Import", f"Not a macropad file: {err}")
            return
        except PadError as err:
            self.disconnected(str(err))
            return
        self.refresh()

    def reset_config(self):
        answer = QMessageBox.question(
            self, "Reset", "Put all keys on all 4 tabs back to the defaults?")
        if answer != QMessageBox.Yes:
            return
        self.dirty = None
        try:
            self.link.reset()
            self.tabs, self.keys = self.link.read_all()
        except PadError as err:
            self.disconnected(str(err))
            return
        self.refresh()

    def closeEvent(self, event):
        self.flush()
        if self.link:
            self.link.close()
        super().closeEvent(event)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port (default: find the pad)")
    args, rest = parser.parse_known_args()
    app = QApplication([sys.argv[0]] + rest)
    window = MainWindow(args.port)
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
