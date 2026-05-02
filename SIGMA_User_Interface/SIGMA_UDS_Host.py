"""
SIGMA_UDS_Host.py  –  UDS UART HOST  (PyQt5 GUI)
ISO-TP framing over 8-byte UART frames:
  SF  [0x0N][payload...]                N = payload length (1-7)
  FF  [0x10][total_len][SID][SUB][d0..d3]
  CF  [0x2N][d0..d6]                   N = sequence number
  FC  [0x30][0x00][0x00][0xAA...]

Requirements:  pip install pyserial PyQt5
"""
import warnings
warnings.filterwarnings("ignore", category=DeprecationWarning)

import sys, time, threading, queue
import serial, serial.tools.list_ports

from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QFrame, QLabel, QPushButton,
    QLineEdit, QComboBox, QVBoxLayout, QHBoxLayout, QStackedWidget,
    QStatusBar, QTreeWidget, QTreeWidgetItem, QHeaderView,
    QAbstractItemView, QStyledItemDelegate, QSizePolicy
)
from PyQt5.QtGui import (
    QFont, QColor, QPalette, QPixmap, QBrush, QPainter, QPen,
    QTextDocument, QPainterPath
)
from PyQt5.QtCore import Qt, pyqtSignal, QTimer, QSize, QRectF

from SIGMA_IO_Control import IOControlDock, SID_IO_CONTROL_POS, SID_IO_CONTROL_REQ
from IOCControlPage   import IOCControlPage

# ═══════════════════════════════════════════════════════════════════════════
FRAME_SIZE = 8
PAD_BYTE   = 0xAA

C = {
    "bg":           "#F5F5F5",
    "panel":        "#ECECEC",
    "header":       "#FFFFFF",
    "border":       "#D0D0D0",
    "accent_red":   "#C0392B",
    "accent_green": "#00963D",
    "text":         "#111111",
    "text_dim":     "#6C757D",
    "row_even":     "#FFFFFF",
    "row_odd":      "#F4F4F4",
    "row_hover":    "#E3F0FF",
    "row_select":   "#D0E8FF",
    "input_bg":     "#FFFFFF",
    "btn_send":     "#00963D",
    "btn_send_fg":  "#FFFFFF",
    "btn_clear":    "#CCCCCC",
    "btn_clear_fg": "#333333",
    "pci":          "#C62828",
    "sid_req":      "#1565C0",
    "sid_resp":     "#E65100",
    "did":          "#00838F",
    "payload":      "#2E7D32",
    "padding":      "#9E9E9E",
    "can_client":   "#00963D",
    "can_ecu":      "#E65100",
    "col_hdr_bg":   "#E0E0E0",
    "col_hdr_fg":   "#444444",
    "legend_bg":    "#F0F0F0",
    "nav_bg":       "#1A1A2E",
    "nav_active":   "#00D4AA",
    "nav_inactive": "#718096",
    "nav_hover":    "#0F3460",
}

BYTE_TAG_COLORS = {k: C[k] for k in
    ("pci","sid_req","sid_resp","did","payload","padding")}

COL_TIME, COL_PROTO, COL_SVC, COL_CAN, COL_BYTES, COL_SENDER, COL_FRAME = range(7)

# ═══════════════════════════════════════════════════════════════════════════
# ISO-TP FRAME BUILDERS  — always 8 bytes
# ═══════════════════════════════════════════════════════════════════════════

def _pad8(data: list) -> bytes:
    data = list(data)[:8]
    while len(data) < 8:
        data.append(PAD_BYTE)
    return bytes(data)

def _build_sf(payload: bytes) -> bytes:
    """[0x0N][payload bytes]"""
    return _pad8([len(payload)] + list(payload))

def _build_ff(payload: bytes) -> bytes:
    """[0x10][total_len][first 6 payload bytes]"""
    return _pad8([0x10, len(payload)] + list(payload[:6]))

def _build_cf(chunk: bytes, sn: int) -> bytes:
    """[0x2N][up to 7 data bytes]"""
    return _pad8([0x20 | (sn & 0x0F)] + list(chunk))

def _build_fc() -> bytes:
    """[0x30][0x00][0x00][0xAA padding]"""
    return _pad8([0x30, 0x00, 0x00])

def build_frame(payload: list) -> bytes:
    """Legacy SF builder — keeps _check_alive and IOC send working."""
    return _build_sf(bytes(payload))

# ═══════════════════════════════════════════════════════════════════════════
# UDS METADATA
# ═══════════════════════════════════════════════════════════════════════════
SERVICE_NAMES = {
    0x10:"DiagnosticSessionControl",  0x11:"ECUReset",
    0x22:"ReadDataByIdentifier",      0x27:"SecurityAccess",
    0x31:"RoutineControl",            0x2F:"InputOutputControlByIdentifier",
    0x50:"DiagnosticSessionControl",  0x51:"ECUReset",
    0x62:"ReadDataByIdentifier",      0x67:"SecurityAccess",
    0x71:"RoutineControl",            0x6F:"InputOutputControlByIdentifier",
    0x7F:"NegativeResponse",
}
SESSION_NAMES  = {0x01:"Default Session",  0x02:"Programming Session", 0x03:"Extended Session"}
RESET_NAMES    = {0x01:"Hard Reset",       0x02:"Key Off/On Reset",    0x03:"Soft Reset"}
SECURITY_NAMES = {0x01:"Request Seed",     0x02:"Send Key",
                  0x03:"Request AES Seed", 0x04:"Send AES Key"}
ROUTINE_NAMES  = {0x01:"Start Routine",    0x02:"Stop Routine",        0x03:"Request Results"}
NRC_NAMES = {
    0x10:"generalReject",            0x11:"serviceNotSupported",
    0x12:"subFunctionNotSupported",  0x13:"incorrectMessageLength",
    0x14:"responseTooLong",          0x22:"conditionsNotCorrect",
    0x24:"requestSequenceError",     0x31:"requestOutOfRange",
    0x33:"securityAccessDenied",     0x35:"invalidKey",
    0x36:"exceededNumberOfAttempts", 0x37:"requiredTimeDelayNotExpired",
    0x72:"generalProgrammingFailure",
}
IOC_CTRL_NAMES = {
    0x00:"ReturnToECU",   0x01:"ResetToDefault",
    0x02:"FreezeCurrent", 0x03:"ShortTermAdjust",
}
IOC_ID_NAMES = {0x0002:"Buzzer", 0x0003:"Fan", 0x0004:"Relay"}

# ═══════════════════════════════════════════════════════════════════════════
# FRAME TYPE DETECTION
# ═══════════════════════════════════════════════════════════════════════════

def _get_frame_type(frame: bytes) -> str:
    pci = frame[0] & 0xF0
    if   pci == 0x00: return "Single Frame (SF)"
    elif pci == 0x10: return "First Frame (FF)"
    elif pci == 0x20: return "Consecutive Frame (CF)"
    elif pci == 0x30: return "Flow Control (FC)"
    else:             return "Unknown"

# ═══════════════════════════════════════════════════════════════════════════
# HELPERS
# ═══════════════════════════════════════════════════════════════════════════
def parse_input(s: str):
    s = s.strip().upper().replace(" ", "")
    if s.startswith("0X"): s = s[2:]
    return [int(s[i:i+2], 16) for i in range(0, len(s), 2) if len(s[i:i+2]) == 2]

def describe_frame(frame: bytes, sender: str):
    """
    Returns (name, detail, can_id, colored_bytes, sender, frame_type).
    Handles SF / FF / CF / FC correctly.
    """
    pci        = frame[0] & 0xF0
    frame_type = _get_frame_type(frame)
    can        = "0x7E8" if sender == "ECU" else "0x7E0"

    # ── Flow Control ──────────────────────────────────────────────────────
    if pci == 0x30:
        fs_map = {0x00:"ContinueToSend", 0x01:"Wait", 0x02:"Overflow"}
        fs     = frame[0] & 0x0F
        colored = [(f"{b:02X}", "pci" if i == 0 else "padding") for i, b in enumerate(frame)]
        return "Flow Control", fs_map.get(fs, f"FS=0x{fs:X}"), can, colored, sender, frame_type

    # ── Consecutive Frame ─────────────────────────────────────────────────
    if pci == 0x20:
        sn = frame[0] & 0x0F
        colored = [(f"{b:02X}", "pci" if i == 0 else "payload") for i, b in enumerate(frame)]
        return "Consecutive Frame", f"SN={sn}", can, colored, sender, frame_type

    # ── First Frame ───────────────────────────────────────────────────────
    if pci == 0x10:
        total_len = frame[1]
        sid       = frame[2] if len(frame) > 2 else 0
        sub       = frame[3] if len(frame) > 3 else 0
        name      = SERVICE_NAMES.get(sid, f"0x{sid:02X}")
        is_resp   = (0x40 <= sid < 0x80) or sid == 0x7F
        det       = SECURITY_NAMES.get(sub, f"Sub 0x{sub:02X}")
        colored = []
        for i, b in enumerate(frame):
            hx = f"{b:02X}"
            if   i == 0: colored.append((hx, "pci"))
            elif i == 1: colored.append((hx, "pci"))          # length byte
            elif i == 2: colored.append((hx, "sid_resp" if is_resp else "sid_req"))
            elif i == 3: colored.append((hx, "payload"))
            elif b == PAD_BYTE: colored.append((hx, "padding"))
            else: colored.append((hx, "payload"))
        return name, det, can, colored, sender, frame_type

    # ── Single Frame (default) ────────────────────────────────────────────
    pci_len = frame[0] & 0x0F
    sid     = frame[1] if len(frame) > 1 else 0
    sub     = frame[2] if len(frame) > 2 else 0
    name    = SERVICE_NAMES.get(sid, f"Unknown(0x{sid:02X})")
    is_resp = (0x40 <= sid < 0x80) or sid == 0x7F

    if sid in (0x2F, 0x6F):
        ctrl = frame[4] if len(frame) > 4 else 0
        det  = IOC_CTRL_NAMES.get(ctrl, f"0x{ctrl:02X}")
    elif sid == 0x7F:
        nrc = frame[3] if len(frame) > 3 else 0
        det = NRC_NAMES.get(nrc, f"0x{nrc:02X}")
    elif sid in (0x10, 0x50): det = SESSION_NAMES.get(sub,  f"0x{sub:02X}")
    elif sid in (0x11, 0x51): det = RESET_NAMES.get(sub,    f"0x{sub:02X}")
    elif sid in (0x27, 0x67): det = SECURITY_NAMES.get(sub, f"Sub 0x{sub:02X}")
    elif sid in (0x31, 0x71): det = ROUTINE_NAMES.get(sub,  f"Sub 0x{sub:02X}")
    elif sid in (0x22, 0x62):
        did = (frame[2] << 8 | frame[3]) if len(frame) > 3 else 0
        det = f"DID 0x{did:04X}"
    else:
        det = "—"

    colored = []
    for i, b in enumerate(frame):
        hx  = f"{b:02X}"
        pos = i - 1
        if   i == 0:                                 colored.append((hx, "pci"))
        elif b == PAD_BYTE and i > pci_len:          colored.append((hx, "padding"))
        elif pos == 0:                               colored.append((hx, "sid_resp" if is_resp else "sid_req"))
        elif sid in (0x22, 0x62) and pos in (1, 2): colored.append((hx, "did"))
        elif sid in (0x2F, 0x6F) and pos in (1, 2): colored.append((hx, "did"))
        elif sid == 0x7F:
            colored.append((hx, "did" if pos == 1 else "sid_resp" if pos == 2 else "payload"))
        else:
            colored.append((hx, "payload"))

    return name, det, can, colored, sender, frame_type

def bytes_html(colored):
    return "&nbsp;".join(
        f'<span style="color:{BYTE_TAG_COLORS.get(t, C["text"])};font-weight:normal;">{h}</span>'
        for h, t in colored)

# ═══════════════════════════════════════════════════════════════════════════
# LOGO
# ═══════════════════════════════════════════════════════════════════════════
def make_logo(size=40) -> QPixmap:
    try:
        src = QPixmap(r"C:\Users\HP\Documents\work_space\UDS\images\logo.jpg")
        if src.isNull():
            raise ValueError
        w, h = src.width(), src.height()
        side = min(w, h)
        src  = src.copy((w - side) // 2, (h - side) // 2, side, side)
        src  = src.scaled(size, size, Qt.IgnoreAspectRatio, Qt.SmoothTransformation)
    except Exception:
        src = QPixmap(size, size)
        src.fill(QColor("#C0392B"))
    result = QPixmap(size, size)
    result.fill(Qt.transparent)
    p = QPainter(result)
    p.setRenderHint(QPainter.Antialiasing)
    path = QPainterPath()
    path.addEllipse(0, 0, size, size)
    p.setClipPath(path)
    p.drawPixmap(0, 0, src)
    p.end()
    return result

# ═══════════════════════════════════════════════════════════════════════════
# HTML DELEGATE
# ═══════════════════════════════════════════════════════════════════════════
class HtmlDelegate(QStyledItemDelegate):
    def paint(self, painter, option, index):
        bg = index.data(Qt.BackgroundRole)
        painter.fillRect(option.rect,
            bg if isinstance(bg, QBrush) else QBrush(QColor(C["row_even"])))
        html = index.data(Qt.UserRole)
        if not html:
            super().paint(painter, option, index)
            return
        doc = QTextDocument()
        doc.setDefaultFont(QFont("Consolas", 9))
        doc.setHtml(f'<span style="font-family:Consolas;font-size:9pt;">{html}</span>')
        painter.save()
        y = option.rect.top() + (option.rect.height() - doc.size().height()) / 2
        painter.translate(option.rect.left() + 6, y)
        doc.drawContents(painter, QRectF(0, 0, option.rect.width() - 8, option.rect.height()))
        painter.restore()
        pen = QPen(QColor(C["border"]))
        pen.setWidth(1)
        painter.setPen(pen)
        painter.drawLine(option.rect.left(), option.rect.bottom(),
                         option.rect.right(), option.rect.bottom())

    def sizeHint(self, option, index):
        return QSize(200, 22)

# ═══════════════════════════════════════════════════════════════════════════
# FACTORIES
# ═══════════════════════════════════════════════════════════════════════════
def _btn(text, bg, fg="#FFF", mw=80, mh=30):
    b = QPushButton(text)
    f = QFont("Segoe UI", 9); f.setBold(True)
    b.setFont(f)
    b.setStyleSheet(f"""
        QPushButton{{background:{bg};color:{fg};border:none;
            border-radius:4px;padding:5px 14px;}}
        QPushButton:hover{{background:{bg}CC;}}
        QPushButton:pressed{{background:{bg}88;}}""")
    b.setMinimumSize(mw, mh)
    b.setCursor(Qt.PointingHandCursor)
    return b

def _lbl(text, fg=None, bold=False, size=9, family="Segoe UI"):
    l = QLabel(text)
    f = QFont(family, size); f.setBold(bold)
    l.setFont(f)
    l.setStyleSheet(f"color:{fg or C['text_dim']};background:transparent;")
    return l

# ═══════════════════════════════════════════════════════════════════════════
# NAV BUTTON
# ═══════════════════════════════════════════════════════════════════════════
class NavButton(QPushButton):
    def __init__(self, icon_text: str, label: str, parent=None):
        super().__init__(parent)
        self._icon_text = icon_text
        self._label     = label
        self._active    = False
        self.setFixedHeight(56)
        self.setMinimumWidth(64)
        self.setCursor(Qt.PointingHandCursor)
        self.setFlat(True)
        self._update_style()

    def set_active(self, v: bool):
        self._active = v
        self._update_style()

    def _update_style(self):
        bg     = C["nav_hover"] if self._active else "transparent"
        border = f"border-left:3px solid {C['nav_active']};" if self._active else "border-left:3px solid transparent;"
        self.setStyleSheet(f"""
            QPushButton{{
                background:{bg};{border}
                border-top:none;border-right:none;border-bottom:none;
                color:{'#FFFFFF' if self._active else C['nav_inactive']};
                padding:0;text-align:center;
            }}
            QPushButton:hover{{background:{C['nav_hover']};color:#FFFFFF;}}
        """)

    def paintEvent(self, event):
        super().paintEvent(event)
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        W, H = self.width(), self.height()
        p.setFont(QFont("Segoe UI", 14))
        p.setPen(QColor(C["nav_active"] if self._active else C["nav_inactive"]))
        p.drawText(QRectF(0, 4, W, H * 0.55), Qt.AlignHCenter | Qt.AlignBottom, self._icon_text)
        p.setFont(QFont("Segoe UI", 7))
        p.setPen(QColor("#FFFFFF" if self._active else C["nav_inactive"]))
        p.drawText(QRectF(0, H * 0.55, W, H * 0.42), Qt.AlignHCenter | Qt.AlignTop, self._label)
        p.end()

# ═══════════════════════════════════════════════════════════════════════════
# MAIN WINDOW
# ═══════════════════════════════════════════════════════════════════════════
class SigmaUDSApp(QMainWindow):
    # Added 7th str arg for frame_type
    _row_sig    = pyqtSignal(float, str, str, str, list, str, str)
    _status_sig = pyqtSignal(str, bool)

    def __init__(self):
        super().__init__()
        self.setWindowTitle("UDS Simulator")
        self.resize(1200, 820)
        self.setMinimumSize(900, 650)
        self._ser          = None
        self._connected    = False
        self._start_time   = None
        self._row_count    = 0
        self._reader_stop  = threading.Event()
        self._current_page = 0

        # ── ISO-TP state ───────────────────────────────────────────────────
        # TX side: queue where reader drops FC frames during an FF send
        self._fc_queue    = queue.Queue()
        self._send_active = False   # True while isotp_send is waiting for FC

        # RX side: reassembly context
        self._isotp_rx = {
            'state':   'IDLE',   # 'IDLE' | 'RECEIVING'
            'total':   0,
            'buf':     bytearray(),
            'sn':      1,
            'ts':      0.0,
        }

        self.setStyleSheet(f"QMainWindow{{background:{C['bg']};}}")
        self._row_sig.connect(self._add_row)
        self._status_sig.connect(self._set_status)
        self._build_ui()
        self._refresh_ports()

        self._timer = QTimer(self)
        self._timer.timeout.connect(self._refresh_ports)
        self._timer.start(2000)

    # ── BUILD UI ───────────────────────────────────────────────────────────
    def _build_ui(self):
        root_w = QWidget()
        root_w.setStyleSheet(f"background:{C['bg']};")
        self.setCentralWidget(root_w)
        main_h = QHBoxLayout(root_w)
        main_h.setContentsMargins(0, 0, 0, 0)
        main_h.setSpacing(0)

        # SIDEBAR
        sidebar = QFrame()
        sidebar.setFixedWidth(72)
        sidebar.setStyleSheet(f"background:{C['nav_bg']};border:none;")
        side_v = QVBoxLayout(sidebar)
        side_v.setContentsMargins(0, 0, 0, 0)
        side_v.setSpacing(0)

        logo_w = QWidget()
        logo_w.setFixedHeight(60)
        logo_w.setStyleSheet(f"background:{C['nav_bg']};")
        logo_l = QHBoxLayout(logo_w)
        logo_l.setContentsMargins(0, 0, 0, 0)
        logo_px = QLabel()
        logo_px.setPixmap(make_logo(36))
        logo_px.setAlignment(Qt.AlignCenter)
        logo_px.setStyleSheet("background:transparent;")
        logo_l.addWidget(logo_px)
        side_v.addWidget(logo_w)

        div = QFrame()
        div.setFixedHeight(1)
        div.setStyleSheet(f"background:{C['nav_hover']};")
        side_v.addWidget(div)

        self._nav_btns = []
        btn_trace = NavButton("=", "Trace")
        btn_trace.clicked.connect(lambda: self._switch_page(0))
        side_v.addWidget(btn_trace)
        self._nav_btns.append(btn_trace)

        btn_ioc = NavButton("*", "I/O Ctrl")
        btn_ioc.clicked.connect(lambda: self._switch_page(1))
        side_v.addWidget(btn_ioc)
        self._nav_btns.append(btn_ioc)

        side_v.addStretch()
        main_h.addWidget(sidebar)

        # RIGHT CONTENT
        right_w = QWidget()
        right_w.setStyleSheet(f"background:{C['bg']};")
        right_v = QVBoxLayout(right_w)
        right_v.setContentsMargins(0, 0, 0, 0)
        right_v.setSpacing(0)
        main_h.addWidget(right_w, stretch=1)

        # HEADER
        hdr = QFrame()
        hdr.setFixedHeight(60)
        hdr.setStyleSheet(f"background:{C['header']};border-bottom:None;")
        hl = QHBoxLayout(hdr)
        hl.setContentsMargins(14, 8, 16, 8)
        hl.setSpacing(8)
        t = QLabel("SIGMA Embedded")
        tf = QFont("Segoe UI", 15); tf.setBold(True)
        t.setFont(tf)
        t.setStyleSheet(f"color:{C['text']};background:transparent;")
        hl.addWidget(t)
        hl.addStretch()
        self._dot = QLabel("●")
        self._dot.setStyleSheet("color:#AAAAAA;background:transparent;font-size:13px;")
        hl.addWidget(self._dot)
        self._conn_lbl = QLabel("Not Connected")
        cf = QFont("Segoe UI", 9); cf.setBold(True)
        self._conn_lbl.setFont(cf)
        self._conn_lbl.setStyleSheet(f"color:{C['text_dim']};background:transparent;")
        hl.addWidget(self._conn_lbl)
        right_v.addWidget(hdr)

        # CONNECTION BAR
        cb = QFrame()
        cb.setStyleSheet(f"background:{C['panel']};border-bottom:1px solid {C['border']};")
        cl = QHBoxLayout(cb)
        cl.setContentsMargins(12, 5, 12, 5)
        cl.setSpacing(8)
        cbo_ss = f"""
            QComboBox{{background:{C['input_bg']};color:{C['text']};
                border:1px solid {C['border']};border-radius:3px;padding:3px 6px;
                font-family:Segoe UI;font-size:8pt;}}
            QComboBox QAbstractItemView{{background:{C['input_bg']};color:{C['text']};
                selection-background-color:{C['border']};}}"""
        cl.addWidget(_lbl("Port:"))
        self._port_combo = QComboBox()
        self._port_combo.setFixedWidth(100)
        self._port_combo.setStyleSheet(cbo_ss)
        cl.addWidget(self._port_combo)
        cl.addWidget(_lbl("Baud:"))
        self._baud_combo = QComboBox()
        self._baud_combo.addItems(["9600","19200","38400","57600","115200","230400","460800","921600"])
        self._baud_combo.setCurrentText("115200")
        self._baud_combo.setFixedWidth(100)
        self._baud_combo.setStyleSheet(cbo_ss)
        cl.addWidget(self._baud_combo)
        self._conn_btn = _btn("Connect", C["btn_send"], C["btn_send_fg"], 110, 30)
        self._conn_btn.clicked.connect(self._toggle_connection)
        cl.addWidget(self._conn_btn)
        self._conn_status = _lbl("● Disconnected", fg=C["accent_red"])
        self._conn_status.setFont(QFont("Segoe UI", 8))
        cl.addWidget(self._conn_status)
        cl.addStretch()
        right_v.addWidget(cb)

        # INPUT BAR
        ib = QFrame()
        ib.setStyleSheet(f"background:{C['panel']};border-bottom:1px solid {C['border']};")
        il = QHBoxLayout(ib)
        il.setContentsMargins(12, 6, 12, 6)
        il.setSpacing(8)
        self._input = QLineEdit()
        self._input.setFont(QFont("Consolas", 9))
        self._input.setPlaceholderText("Enter UDS Request")
        self._input.setStyleSheet(f"""
            QLineEdit{{background:{C['input_bg']};color:{C['text']};
                border:1px solid {C['border']};border-radius:3px;padding:6px 10px;}}
            QLineEdit:focus{{border:1px solid {C['accent_green']};}}""")
        self._input.returnPressed.connect(self._send_request)
        il.addWidget(self._input, stretch=1)
        sb = _btn("Send Request", C["btn_send"], C["btn_send_fg"], 130, 32)
        sb.clicked.connect(self._send_request)
        il.addWidget(sb)
        clr = _btn("Clear", C["btn_clear"], C["btn_clear_fg"], 70, 32)
        clr.clicked.connect(self._clear_trace)
        il.addWidget(clr)
        right_v.addWidget(ib)

        # STACKED PAGES
        self._stack = QStackedWidget()
        self._stack.setStyleSheet("background:transparent;")
        right_v.addWidget(self._stack, stretch=1)

        # PAGE 0: TRACE
        trace_page = QWidget()
        trace_page.setStyleSheet(f"background:{C['bg']};")
        tp_v = QVBoxLayout(trace_page)
        tp_v.setContentsMargins(0, 0, 0, 0)
        tp_v.setSpacing(0)

        leg = QFrame()
        leg.setFixedHeight(28)
        leg.setStyleSheet(f"background:{C['legend_bg']};border-bottom:1px solid {C['border']};")
        ll = QHBoxLayout(leg)
        ll.setContentsMargins(12, 0, 12, 0)
        ll.setSpacing(4)
        ll.addWidget(_lbl("Trace", fg=C["text"], bold=True))
        ll.addStretch()
        for lname, lcol in [("PCI", C["pci"]), ("SID REQ", C["sid_req"]), ("DID", C["did"]),
                             ("SID RESP", C["sid_resp"]), ("PAYLOAD", C["payload"]),
                             ("PADDING", C["padding"])]:
            x = QLabel(f"  {lname}")
            x.setFont(QFont("Segoe UI", 8))
            x.setStyleSheet(f"color:{lcol};background:transparent;padding:0 6px;")
            ll.addWidget(x)
        tp_v.addWidget(leg)

        self._tree = QTreeWidget()
        self._tree.setContextMenuPolicy(Qt.CustomContextMenu)
        self._tree.customContextMenuRequested.connect(self._on_tree_context_menu)
        self._tree.setColumnCount(7)
        self._tree.setHeaderLabels([
            "Time", "Protocol Service", "Service",
            "CAN ID (HEX)", "Data Bytes (HEX)", "Sender", "Frame Type"])
        self._tree.setRootIsDecorated(False)
        self._tree.setAlternatingRowColors(True)
        self._tree.setSelectionBehavior(QAbstractItemView.SelectRows)
        self._tree.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self._tree.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self._tree.setUniformRowHeights(True)
        self._tree.setIndentation(0)
        self._tree.setItemDelegateForColumn(COL_BYTES, HtmlDelegate(self._tree))

        hh = self._tree.header()
        hh.setSectionResizeMode(COL_TIME,   QHeaderView.ResizeToContents)
        hh.setSectionResizeMode(COL_PROTO,  QHeaderView.ResizeToContents)
        hh.setSectionResizeMode(COL_SVC,    QHeaderView.ResizeToContents)
        hh.setSectionResizeMode(COL_CAN,    QHeaderView.ResizeToContents)
        hh.setSectionResizeMode(COL_BYTES,  QHeaderView.Stretch)
        hh.setSectionResizeMode(COL_SENDER, QHeaderView.ResizeToContents)
        hh.setSectionResizeMode(COL_FRAME,  QHeaderView.ResizeToContents)
        hh.setSectionsMovable(False) 
        hh.setStretchLastSection(False)
        hh.setMinimumSectionSize(70)

        self._tree.setStyleSheet(f"""
            QTreeWidget{{
                background:{C['bg']};alternate-background-color:{C['row_odd']};
                border:none;outline:none;font-family:Consolas;font-size:9pt;}}
            QTreeWidget::item{{
                height:24px;border-bottom:1px solid {C['border']};padding:0 4px;}}
            QTreeWidget::item:hover{{background:{C['row_hover']};}}
            QTreeWidget::item:selected{{background:{C['row_select']};color:{C['text']};}}
            QHeaderView::section{{
                background:{C['col_hdr_bg']};color:{C['col_hdr_fg']};
                font-family:Segoe UI;font-size:8pt;font-weight:bold;
                padding:4px 8px;border:none;
                border-right:1px solid {C['border']};
                border-bottom:2px solid {C['border']};}}
            QScrollBar:vertical{{background:{C['panel']};width:8px;border:none;}}
            QScrollBar::handle:vertical{{background:{C['border']};border-radius:4px;min-height:20px;}}
            QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{{height:0;}}
            QScrollBar:horizontal{{background:{C['panel']};height:8px;border:none;}}
            QScrollBar::handle:horizontal{{background:{C['border']};border-radius:4px;min-width:20px;}}
            QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{{width:0;}}""")
        tp_v.addWidget(self._tree, stretch=1)
        self._stack.addWidget(trace_page)

        # PAGE 1: IOC CONTROL
        self._ioc_dock = IOControlDock()
        self._ioc_page = IOCControlPage(self._ioc_dock)
        self._ioc_page.send_frame_sig.connect(self._send_hex_string)
        self._stack.addWidget(self._ioc_page)

        # STATUS BAR
        self._sb = QStatusBar()
        self._sb.setStyleSheet(f"""
            QStatusBar{{background:{C['header']};color:{C['text_dim']};
                font-family:Segoe UI;font-size:8pt;
                border-top:1px solid {C['border']};}}""")
        self._sb.showMessage("Ready")
        self.setStatusBar(self._sb)

        self._switch_page(0)
    # ── COPY DATA ─────────────────────────────────────────────────────
    def _on_tree_context_menu(self, pos):
        from PyQt5.QtWidgets import QMenu
        item = self._tree.itemAt(pos)
        if not item:
            return
        menu = QMenu(self._tree)
        act = menu.addAction("Copy Data Bytes")
        act.triggered.connect(
            lambda: QApplication.clipboard().setText(item.text(COL_BYTES))
        )
        menu.exec_(self._tree.viewport().mapToGlobal(pos))
    # ── PAGE SWITCHING ─────────────────────────────────────────────────────
    def _switch_page(self, idx: int):
        self._current_page = idx
        self._stack.setCurrentIndex(idx)
        for i, btn in enumerate(self._nav_btns):
            btn.set_active(i == idx)

    # ── PORT REFRESH ───────────────────────────────────────────────────────
    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        cur   = self._port_combo.currentText()
        self._port_combo.blockSignals(True)
        self._port_combo.clear()
        self._port_combo.addItems(ports)
        if cur in ports:
            self._port_combo.setCurrentText(cur)
        elif ports:
            self._port_combo.setCurrentText(ports[0])
        self._port_combo.blockSignals(False)

    # ── CONNECTION ─────────────────────────────────────────────────────────
    def _toggle_connection(self):
        if self._connected:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port = self._port_combo.currentText().strip()
        if not port:
            self._set_status("No port selected!", error=True)
            return
        try:
            baud = int(self._baud_combo.currentText())
        except ValueError:
            self._set_status("Invalid baud rate", error=True)
            return
        try:
            self._ser = serial.Serial(port, baud, timeout=1, write_timeout=1,
                                      dsrdtr=False, rtscts=False)
            time.sleep(0.15)
            self._ser.reset_input_buffer()
            self._ser.reset_output_buffer()
            alive = self._check_alive()
            self._connected  = True
            self._start_time = time.monotonic()

            self._reader_stop.clear()
            threading.Thread(target=self._reader_thread, daemon=True).start()

            self._conn_btn.setStyleSheet(f"""
                QPushButton{{background:{C['accent_red']};color:#FFF;
                    border:none;border-radius:4px;padding:5px 14px;}}
                QPushButton:hover{{background:#A02020;}}""")
            self._conn_btn.setText("Disconnect")
            sc = C["accent_green"] if alive else "#E67E00"
            st = f"● Connected  {port} @ {baud}" + ("" if alive else "  (no ECU response)")
            self._conn_status.setText(st)
            self._conn_status.setStyleSheet(f"color:{sc};background:transparent;")
            self._dot.setStyleSheet(f"color:{sc};background:transparent;font-size:13px;")
            self._conn_lbl.setText("Connected" if alive else "Connected - ECU silent")
            self._conn_lbl.setStyleSheet(f"color:{sc};background:transparent;")
            self._set_status(f"Port {port} opened at {baud} baud" +
                             ("" if alive else "  -- ECU did not respond to ping"))
        except serial.SerialException as e:
            if self._ser:
                try: self._ser.close()
                except Exception: pass
                self._ser = None
            self._set_status(f"Cannot open {port}: {e}", error=True)

    def _check_alive(self) -> bool:
        try:
            self._ser.write(build_frame([0x10, 0x01]))
            deadline = time.monotonic() + 0.4
            buf = b""
            while time.monotonic() < deadline:
                chunk = self._ser.read(FRAME_SIZE - len(buf))
                if chunk: buf += chunk
                if len(buf) >= FRAME_SIZE: return True
            return False
        except Exception:
            return False

    def _disconnect(self):
        self._reader_stop.set()
        time.sleep(0.05)
        if self._ser:
            try: self._ser.close()
            except Exception: pass
            self._ser = None
        self._connected = False
        self._send_active = False
        self._isotp_rx = {'state':'IDLE','total':0,'buf':bytearray(),'sn':1,'ts':0.0}
        self._conn_btn.setStyleSheet(f"""
            QPushButton{{background:{C['btn_send']};color:{C['btn_send_fg']};
                border:none;border-radius:4px;padding:5px 14px;}}
            QPushButton:hover{{background:#00A870;}}""")
        self._conn_btn.setText("Connect")
        self._conn_status.setText("● Disconnected")
        self._conn_status.setStyleSheet(f"color:{C['accent_red']};background:transparent;")
        self._dot.setStyleSheet("color:#AAAAAA;background:transparent;font-size:13px;")
        self._conn_lbl.setText("Not Connected")
        self._conn_lbl.setStyleSheet(f"color:{C['text_dim']};background:transparent;")
        self._set_status("Disconnected")

    # ═══════════════════════════════════════════════════════════════════════
    # LOG HELPER — thread-safe, emits signal
    # ═══════════════════════════════════════════════════════════════════════
    def _log_frame(self, frame: bytes, sender: str):
        """Emit a row signal from any thread."""
        if self._start_time is None:
            return
        t = round(time.monotonic() - self._start_time, 3)
        self._row_sig.emit(t, *describe_frame(frame, sender))

    # ═══════════════════════════════════════════════════════════════════════
    # ISO-TP SEND  (runs in worker thread)
    # ═══════════════════════════════════════════════════════════════════════
    def _isotp_send(self, payload: bytes):
        """
        Smart sender — SF for ≤7 bytes, FF+CFs for longer.
        Runs in a background thread to allow FC waiting without blocking GUI.
        """
        try:
            if len(payload) <= 7:
                # ── Single Frame ───────────────────────────────────────────
                frame = _build_sf(payload)
                self._ser.write(frame)
                self._log_frame(frame, "Client")
            else:
                # ── First Frame ────────────────────────────────────────────
                ff = _build_ff(payload)
                self._send_active = True
                self._ser.write(ff)
                self._log_frame(ff, "Client")

                # Wait for ECU's FC (reader thread puts it in queue)
                try:
                    fc = self._fc_queue.get(timeout=2.0)
                    # FC is already logged by reader thread
                except queue.Empty:
                    self._status_sig.emit("FC timeout — ECU did not respond", True)
                    self._send_active = False
                    return

                self._send_active = False

                # Validate FC
                if (fc[0] & 0xF0) != 0x30 or (fc[0] & 0x0F) != 0x00:
                    self._status_sig.emit(f"Bad FC: 0x{fc[0]:02X}", True)
                    return

                # ── Consecutive Frames ─────────────────────────────────────
                offset = 6
                sn     = 1
                while offset < len(payload):
                    chunk = payload[offset:offset + 7]
                    cf    = _build_cf(chunk, sn)
                    self._ser.write(cf)
                    self._log_frame(cf, "Client")
                    time.sleep(0.01)   # small gap between CFs
                    offset += 7
                    sn      = (sn + 1) & 0x0F

            self._status_sig.emit(
                f"Sent {len(payload)} bytes → "
                f"{'SF' if len(payload) <= 7 else 'FF+CFs'}", False)

        except serial.SerialException as e:
            self._status_sig.emit(f"Send error: {e}", True)
            self._send_active = False

    # ═══════════════════════════════════════════════════════════════════════
    # SEND REQUEST  (called from GUI thread)
    # ═══════════════════════════════════════════════════════════════════════
    def _send_hex_string(self, hex_str: str):
        self._input.setText(hex_str)
        self._send_request()

    def _send_request(self):
        raw = self._input.text().strip()
        if not raw:
            return

        payload_list = parse_input(raw)
        if not payload_list:
            self._set_status("Invalid hex input", error=True)
            return

        payload = bytes(payload_list)

        # Update IOC dashboard always
        tx = build_frame(payload_list)
        self._ioc_dock.process_frame(tx, "Client")

        self._input.clear()

        if not self._connected or not self._ser:
            self._set_status("Dashboard updated (not sent — no connection)")
            return

        # Dispatch in thread so FC-waiting doesn't block GUI
        threading.Thread(
            target=self._isotp_send,
            args=(payload,),
            daemon=True
        ).start()

    # ═══════════════════════════════════════════════════════════════════════
    # READER THREAD  — handles SF / FF / CF / FC from ECU
    # ═══════════════════════════════════════════════════════════════════════
    def _reader_thread(self):
        buf = b""
        while not self._reader_stop.is_set():
            try:
                if not self._ser or not self._ser.is_open:
                    break
                waiting = self._ser.in_waiting
                if waiting == 0:
                    time.sleep(0.005)
                    continue
                chunk = self._ser.read(min(waiting, FRAME_SIZE - len(buf)))
                if chunk:
                    buf += chunk

                while len(buf) >= FRAME_SIZE:
                    frame = bytes(buf[:FRAME_SIZE])
                    buf   = buf[FRAME_SIZE:]
                    pci   = frame[0] & 0xF0

                    # ── Flow Control (ECU → Tester, during our FF send) ────
                    if pci == 0x30:
                        self._log_frame(frame, "ECU")
                        if self._send_active:
                            # Pass FC to the isotp_send thread
                            self._fc_queue.put(frame)
                        # else: unsolicited FC, just log it

                    # ── First Frame from ECU ───────────────────────────────
                    elif pci == 0x10:
                        # Log the FF
                        self._log_frame(frame, "ECU")
                        # Start reassembly
                        total_len = frame[1]
                        ctx = self._isotp_rx
                        ctx['state'] = 'RECEIVING'
                        ctx['total'] = total_len
                        ctx['buf']   = bytearray(frame[2:8])  # first 6 payload bytes
                        ctx['sn']    = 1
                        ctx['ts']    = time.monotonic()
                        # Send FC back to ECU
                        fc = _build_fc()
                        self._ser.write(fc)
                        self._log_frame(fc, "Client")   # log our FC send

                    # ── Consecutive Frame from ECU ─────────────────────────
                    elif pci == 0x20:
                        self._log_frame(frame, "ECU")
                        ctx = self._isotp_rx
                        if ctx['state'] != 'RECEIVING':
                            # Unexpected CF — ignore
                            continue

                        # CF timeout guard (1 second between CFs)
                        if (time.monotonic() - ctx['ts']) > 1.0:
                            ctx['state'] = 'IDLE'
                            self._status_sig.emit("ISO-TP CF timeout", True)
                            continue

                        sn = frame[0] & 0x0F
                        if sn != ctx['sn']:
                            ctx['state'] = 'IDLE'
                            self._status_sig.emit(
                                f"ISO-TP SN error: expected {ctx['sn']}, got {sn}", True)
                            continue

                        # Append data
                        remaining = ctx['total'] - len(ctx['buf'])
                        ctx['buf'] += frame[1:1 + min(7, remaining)]
                        ctx['sn']   = (ctx['sn'] + 1) & 0x0F
                        ctx['ts']   = time.monotonic()

                        # Reassembly complete?
                        if len(ctx['buf']) >= ctx['total']:
                            ctx['state'] = 'IDLE'
                            
                    # ── Single Frame from ECU ──────────────────────────────
                    else:
                        self._log_frame(frame, "ECU")
                        if frame[1] == SID_IO_CONTROL_POS:
                            self._ioc_dock.process_frame(frame, "ECU")

            except serial.SerialException:
                self._status_sig.emit("Connection lost", True)
                break
            except Exception as e:
                self._status_sig.emit(f"Reader error: {e}", True)
                break

        if self._connected:
            self._status_sig.emit("Port disconnected unexpectedly", True)
            self._connected = False

    # ── ADD ROW ────────────────────────────────────────────────────────────
    def _add_row(self, t, name, svc, can, colored, sender, frame_type):
        is_client = sender == "Client"
        is_nrc    = name   == "NegativeResponse"
        row_bg    = QColor(C["row_even"] if self._row_count % 2 == 0 else C["row_odd"])
        self._row_count += 1

        item = QTreeWidgetItem(self._tree)
        for c in range(7):
            item.setBackground(c, QBrush(row_bg))

        f8 = QFont("Segoe UI", 8)
        f8.setWeight(QFont.Normal)

        item.setText(COL_TIME, f"{t % 10.0:.3f}")
        item.setForeground(COL_TIME, QBrush(QColor(C["text"])))
        item.setFont(COL_TIME, f8)
        item.setTextAlignment(COL_TIME, Qt.AlignRight | Qt.AlignVCenter)

        item.setText(COL_PROTO, name)
        item.setForeground(COL_PROTO, QBrush(QColor(C["pci"] if is_nrc else C["text"])))
        item.setFont(COL_PROTO, f8)

        item.setText(COL_SVC, svc)
        item.setForeground(COL_SVC, QBrush(QColor(C["pci"] if is_nrc else C["text"])))
        item.setFont(COL_SVC, f8)

        item.setText(COL_CAN, can)
        item.setForeground(COL_CAN, QBrush(QColor(C["can_client"] if is_client else C["can_ecu"])))
        item.setFont(COL_CAN, f8)
        item.setTextAlignment(COL_CAN, Qt.AlignCenter | Qt.AlignVCenter)

        html  = bytes_html(colored)
        plain = " ".join(h for h, _ in colored)
        item.setData(COL_BYTES, Qt.UserRole, html)
        item.setData(COL_BYTES, Qt.BackgroundRole, QBrush(row_bg))
        item.setText(COL_BYTES, plain)
        item.setFont(COL_BYTES, f8)

        item.setText(COL_SENDER, "DiagBox" if is_client else "ECU")
        item.setForeground(COL_SENDER, QBrush(QColor(C["text"])))
        item.setFont(COL_SENDER, f8)
        item.setTextAlignment(COL_SENDER, Qt.AlignCenter | Qt.AlignVCenter)

        # Frame type — color-coded
        item.setText(COL_FRAME, frame_type)
        item.setForeground(COL_FRAME, QBrush(QColor(C["text"])))
        item.setFont(COL_FRAME, f8)
        item.setTextAlignment(COL_FRAME, Qt.AlignCenter | Qt.AlignVCenter)

        self._tree.scrollToItem(item)

    # ── MISC ───────────────────────────────────────────────────────────────
    def _clear_trace(self):
        self._tree.clear()
        self._row_count = 0
        self._set_status("Trace cleared")

    def _set_status(self, msg: str, error: bool = False):
        col = C["accent_red"] if error else C["text_dim"]
        self._sb.setStyleSheet(f"""
            QStatusBar{{background:{C['header']};color:{col};
                font-family:Segoe UI;font-size:8pt;
                border-top:1px solid {C['border']};}}""")
        self._sb.showMessage(msg)

    def closeEvent(self, event):
        self._reader_stop.set()
        if self._ser:
            try: self._ser.close()
            except Exception: pass
        event.accept()


# ═══════════════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    pal = QPalette()
    pal.setColor(QPalette.Window,          QColor(C["bg"]))
    pal.setColor(QPalette.WindowText,      QColor(C["text"]))
    pal.setColor(QPalette.Base,            QColor(C["input_bg"]))
    pal.setColor(QPalette.AlternateBase,   QColor(C["row_odd"]))
    pal.setColor(QPalette.Text,            QColor(C["text"]))
    pal.setColor(QPalette.Button,          QColor(C["panel"]))
    pal.setColor(QPalette.ButtonText,      QColor(C["text"]))
    pal.setColor(QPalette.Highlight,       QColor("#BBDEFB"))
    pal.setColor(QPalette.HighlightedText, QColor(C["text"]))
    app.setPalette(pal)
    win = SigmaUDSApp()
    win.show()
    sys.exit(app.exec_())