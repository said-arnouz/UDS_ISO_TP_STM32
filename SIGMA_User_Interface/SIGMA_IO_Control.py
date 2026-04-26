"""
SIGMA_IO_Control.py  –  ECU State Dashboard  (SID 0x2F / 0x6F)
"""

from PyQt5.QtWidgets import QWidget, QHBoxLayout, QVBoxLayout, QLabel, QFrame, QSizePolicy
from PyQt5.QtCore    import Qt, QRectF, QTimer
from PyQt5.QtGui     import QPainter, QColor, QPen, QFont, QBrush

# ── UDS constants ──────────────────────────────────────────────────────────────
SID_IO_CONTROL_REQ        = 0x2F
SID_IO_CONTROL_POS        = 0x6F
IOC_ID_BUZZER             = 0x0002
IOC_ID_FAN                = 0x0003
IOC_ID_RELAY              = 0x0004
IO_CTRL_RETURN_TO_ECU     = 0x00
IO_CTRL_RESET_TO_DEFAULT  = 0x01
IO_CTRL_FREEZE_CURRENT    = 0x02
IO_CTRL_SHORT_TERM_ADJUST = 0x03
PAD_BYTE                  = 0xAA
FAN_DEFAULT               = 30
BUZZER_DEFAULT            = 0

# Per-signal value ceilings (raw byte → display %)
# Values above ceiling are clamped to ceiling (displayed as 100 %)
# Relay has no ceiling entry — it is binary (0x01=CLOSED, else OPEN)
VALUE_MAX = {
    IOC_ID_BUZZER: 100,
    IOC_ID_FAN:    100,
}

# ── palette ────────────────────────────────────────────────────────────────────
_BG     = "#1A1A2E"
_CARD   = "#16213E"
_BORDER = "#0F3460"
_ACCENT = "#00D4AA"
_RED    = "#E94560"
_TRACK  = "#374151"
_TXT_HI = "#E2E8F0"
_TXT_LO = "#718096"
_YELLOW = "#F6C90E"
_BLUE   = "#4299E1"

_NO_VALUE = -1   # sentinel: frame carries no value byte


# ══════════════════════════════════════════════════════════════════════════════
# ArcGauge
# ══════════════════════════════════════════════════════════════════════════════
class ArcGauge(QWidget):
    def __init__(self, color: str, max_val: int = 100, parent=None):
        super().__init__(parent)
        self._value    = 0.0
        self._target   = 0.0
        self._color    = QColor(color)
        self._override = False
        self._max_val  = max(1, max_val)
        self.setMinimumSize(110, 110)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        self._anim = QTimer(self)
        self._anim.setInterval(16)
        self._anim.timeout.connect(self._step)

    def set_value(self, v: int, override: bool = True):
        clamped        = max(0, min(self._max_val, v))
        self._target   = float(clamped)
        self._override = override
        if not self._anim.isActive():
            self._anim.start()

    def set_ecu_control(self):
        self._override = False
        self.update()

    def _step(self):
        diff = self._target - self._value
        if abs(diff) < 1.0:
            self._value = self._target
            self._anim.stop()
        else:
            self._value += 2.0 if diff > 0 else -2.0
        self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)

        W, H   = self.width(), self.height()
        m      = 14
        side   = min(W, H) - m * 2
        cx, cy = W / 2, H / 2 + 4
        r      = side / 2
        thick  = max(7, int(r * 0.14))

        rect  = QRectF(cx - r, cy - r, r * 2, r * 2)
        START = 220
        SPAN  = 260

        p.setPen(QPen(QColor(_TRACK), thick, Qt.SolidLine, Qt.RoundCap))
        p.setBrush(Qt.NoBrush)
        p.drawArc(rect, int(START * 16), int(-SPAN * 16))

        fraction = self._value / self._max_val
        filled   = SPAN * fraction
        if filled > 0.5:
            arc_color = self._color if self._override else QColor(_TRACK).lighter(145)
            p.setPen(QPen(arc_color, thick, Qt.SolidLine, Qt.RoundCap))
            p.drawArc(rect, int(START * 16), int(-filled * 16))

        val_str = f"{int(round(self._value))}%"
        fsize   = max(11, int(r * 0.42))
        f = QFont("Consolas", fsize); f.setBold(True)
        p.setFont(f)
        txt_color = self._color if self._override else QColor(_TXT_LO)
        p.setPen(txt_color)
        p.drawText(QRectF(cx - r, cy - r * 0.5, r * 2, r * 1.0),
                   Qt.AlignCenter, val_str)
        p.end()


# ══════════════════════════════════════════════════════════════════════════════
# RelayWidget
# ══════════════════════════════════════════════════════════════════════════════
class RelayWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._closed   = False
        self._override = False
        self.setMinimumSize(110, 110)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

    def set_state(self, closed: bool, override: bool = True):
        self._closed   = closed
        self._override = override
        self.update()

    def set_ecu_control(self):
        self._override = False
        self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)

        W, H   = self.width(), self.height()
        cx, cy = W / 2, H / 2 + 4
        r      = min(W, H) / 2 - 14
        ri     = r - 10

        color = QColor(_RED) if self._closed else QColor(_TRACK)

        p.setPen(QPen(color, 3))
        p.setBrush(Qt.NoBrush)
        p.drawEllipse(QRectF(cx - r, cy - r, r * 2, r * 2))

        inner_fill = color.darker(140) if self._closed else QColor(_CARD)
        p.setBrush(QBrush(inner_fill))
        p.setPen(Qt.NoPen)
        p.drawEllipse(QRectF(cx - ri, cy - ri, ri * 2, ri * 2))

        txt   = "CLOSED" if self._closed else "OPEN"
        c_txt = QColor(_RED) if self._closed else QColor(_TXT_LO)
        fsize = max(9, int(ri * 0.38))
        f = QFont("Consolas", fsize); f.setBold(True)
        p.setFont(f)
        p.setPen(c_txt)
        p.drawText(QRectF(cx - ri, cy - ri * 0.5, ri * 2, ri * 1.0),
                   Qt.AlignCenter, txt)
        p.end()


# ══════════════════════════════════════════════════════════════════════════════
# SignalTile
# ══════════════════════════════════════════════════════════════════════════════
class SignalTile(QFrame):
    def __init__(self, title: str, inner: QWidget, parent=None):
        super().__init__(parent)
        self.setStyleSheet(f"""
            QFrame {{
                background:{_CARD};
                border:1px solid {_BORDER};
                border-radius:8px;
            }}
        """)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(8, 6, 8, 6)
        lay.setSpacing(3)

        hrow = QHBoxLayout(); hrow.setSpacing(4)
        tl = QLabel(title)
        tf = QFont("Segoe UI", 8); tf.setBold(True)
        tl.setFont(tf)
        tl.setStyleSheet(f"color:{_TXT_HI};background:transparent;")
        hrow.addWidget(tl)
        hrow.addStretch()

        self._status_lbl = QLabel("ECU ctrl")
        self._status_lbl.setFont(QFont("Segoe UI", 7))
        self._status_lbl.setStyleSheet(f"color:{_TXT_LO};background:transparent;")
        hrow.addWidget(self._status_lbl)
        lay.addLayout(hrow)
        lay.addWidget(inner, stretch=1)

    def set_status(self, override: bool):
        if override:
            self._status_lbl.setText("Tester override")
            self._status_lbl.setStyleSheet(
                f"color:{_ACCENT};background:transparent;font-size:7pt;")
        else:
            self._status_lbl.setText("ECU ctrl")
            self._status_lbl.setStyleSheet(
                f"color:{_TXT_LO};background:transparent;font-size:7pt;")


# ══════════════════════════════════════════════════════════════════════════════
# IOControlDock  –  tiles only, no header bar
# ══════════════════════════════════════════════════════════════════════════════
class IOControlDock(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setStyleSheet(f"background:{_BG};")
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.setFixedHeight(160)   # reduced: no header row any more
        self._build()

    def _build(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(10, 6, 10, 6)
        root.setSpacing(0)

        tiles = QHBoxLayout()
        tiles.setSpacing(8)

        self._buzzer_gauge = ArcGauge(_YELLOW, max_val=VALUE_MAX[IOC_ID_BUZZER])
        self._buzzer_tile  = SignalTile("Buzzer  ·  TIM3 CH1", self._buzzer_gauge)

        self._fan_gauge = ArcGauge(_BLUE, max_val=VALUE_MAX[IOC_ID_FAN])
        self._fan_tile  = SignalTile("Fan  ·  TIM2 CH2", self._fan_gauge)

        self._relay_widget = RelayWidget()
        self._relay_tile   = SignalTile("Relay  ·  PB0  GPIO", self._relay_widget)

        tiles.addWidget(self._buzzer_tile)
        tiles.addWidget(self._fan_tile)
        tiles.addWidget(self._relay_tile)
        root.addLayout(tiles, stretch=1)

    def _dispatch(self, frame: bytes, is_request: bool):
        if len(frame) < 5:
            return

        sid_byte = frame[1]
        if sid_byte not in (SID_IO_CONTROL_REQ, SID_IO_CONTROL_POS):
            return

        ioc_id     = (frame[2] << 8) | frame[3]
        ctrl_param = frame[4]

        if len(frame) > 5:
            raw_val = frame[5]
            ceiling = VALUE_MAX.get(ioc_id, 100)
            value   = max(0, min(ceiling, raw_val))
        else:
            raw_val = _NO_VALUE
            value   = 0

        override = ctrl_param != IO_CTRL_RETURN_TO_ECU

        if ioc_id == IOC_ID_BUZZER:
            if ctrl_param == IO_CTRL_SHORT_TERM_ADJUST:
                self._buzzer_gauge.set_value(value, override=True)
            elif ctrl_param == IO_CTRL_RESET_TO_DEFAULT:
                self._buzzer_gauge.set_value(BUZZER_DEFAULT, override=True)
            elif ctrl_param == IO_CTRL_FREEZE_CURRENT:
                pass
            elif ctrl_param == IO_CTRL_RETURN_TO_ECU:
                self._buzzer_gauge.set_ecu_control()
            self._buzzer_tile.set_status(override)

        elif ioc_id == IOC_ID_FAN:
            if ctrl_param == IO_CTRL_SHORT_TERM_ADJUST:
                self._fan_gauge.set_value(value, override=True)
            elif ctrl_param == IO_CTRL_RESET_TO_DEFAULT:
                self._fan_gauge.set_value(FAN_DEFAULT, override=True)
            elif ctrl_param == IO_CTRL_FREEZE_CURRENT:
                pass
            elif ctrl_param == IO_CTRL_RETURN_TO_ECU:
                self._fan_gauge.set_ecu_control()
            self._fan_tile.set_status(override)

        elif ioc_id == IOC_ID_RELAY:
            if ctrl_param == IO_CTRL_SHORT_TERM_ADJUST:
                if raw_val != _NO_VALUE:
                    self._relay_widget.set_state(raw_val == 0x01, override=True)
            elif ctrl_param == IO_CTRL_RESET_TO_DEFAULT:
                self._relay_widget.set_state(False, override=True)
            elif ctrl_param == IO_CTRL_FREEZE_CURRENT:
                pass
            elif ctrl_param == IO_CTRL_RETURN_TO_ECU:
                self._relay_widget.set_ecu_control()
            self._relay_tile.set_status(override)

    def process_frame(self, frame: bytes, sender: str):
        is_req = (sender == "Client")
        QTimer.singleShot(0, lambda: self._dispatch(frame, is_req))

    def process_ecu_response(self, frame: bytes):
        QTimer.singleShot(0, lambda: self._dispatch(frame, False))