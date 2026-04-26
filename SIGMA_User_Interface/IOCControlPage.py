"""
IOCControlPage.py  –  I/O Control Page Widget
"""

from PyQt5.QtWidgets import QWidget, QVBoxLayout
from PyQt5.QtCore    import pyqtSignal

from SIGMA_IO_Control import IOControlDock


class IOCControlPage(QWidget):
    send_frame_sig = pyqtSignal(str)

    _BG = "#1A1A2E"

    def __init__(self, ioc_dock: IOControlDock, parent=None):
        super().__init__(parent)
        self._ioc_dock = ioc_dock
        self.setStyleSheet(f"background:{self._BG};")
        self._build()

    def _build(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(16, 12, 16, 12)
        root.setSpacing(0)

        # Just the dashboard — no title row, no subtitle
        root.addWidget(self._ioc_dock)
        root.addStretch()