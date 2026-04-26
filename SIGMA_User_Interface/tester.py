"""
SIGMA UDS Host — Professional tester for STM32 ECU over UART.

ISO-TP framing rules (8-byte UART frames):
  SF  [0x0N][payload...]                N = payload length (1-7)
  FF  [0x10][total_len][payload[0..5]]
  CF  [0x2N][payload...]                N = sequence number (1..F)
  FC  [0x30][0x00][0x00][0xAA...]       ContinueToSend, BS=0, STmin=0

FC rule: whoever RECEIVES a FF sends the FC back.
  - Tester sends FF → ECU sends FC → Tester sends CFs
  - ECU sends FF   → Tester sends FC → ECU sends CFs
"""

import serial
import sys
import time
from typing import Optional

# ── Configuration ──────────────────────────────────────────────────────────────
PORT     = "COM4"
BAUDRATE = 115200
TIMEOUT  = 2          # seconds, for each rx() call

# ── Serial connection ──────────────────────────────────────────────────────────
try:
    ser = serial.Serial(PORT, BAUDRATE, timeout=TIMEOUT)
    print(f"[OK] Connected to {PORT} @ {BAUDRATE} baud\n")
except serial.SerialException as e:
    print(f"[ERR] Cannot open {PORT}: {e}")
    sys.exit(1)


# ══════════════════════════════════════════════════════════════════════════════
#  Frame builders — always produce exactly 8 bytes
# ══════════════════════════════════════════════════════════════════════════════

def _pad8(data: list) -> bytes:
    """Pad or truncate list to exactly 8 bytes with 0xAA filler."""
    data = data[:8]
    while len(data) < 8:
        data.append(0xAA)
    return bytes(data)

def _build_sf(payload: bytes) -> bytes:
    """Single Frame: [0x0N][payload bytes...]"""
    assert 1 <= len(payload) <= 7, f"SF payload must be 1-7 bytes, got {len(payload)}"
    return _pad8([len(payload)] + list(payload))

def _build_ff(payload: bytes) -> bytes:
    """First Frame: [0x10][total_len][first 6 payload bytes]"""
    return _pad8([0x10, len(payload)] + list(payload[:6]))

def _build_cf(chunk: bytes, sn: int) -> bytes:
    """Consecutive Frame: [0x2N][up to 7 data bytes]"""
    return _pad8([0x20 | (sn & 0x0F)] + list(chunk))

def _build_fc() -> bytes:
    """Flow Control (ContinueToSend): [0x30][0x00][0x00][0xAA...]"""
    return _pad8([0x30, 0x00, 0x00])


# ══════════════════════════════════════════════════════════════════════════════
#  Low-level UART TX / RX
# ══════════════════════════════════════════════════════════════════════════════

def _tx(frame: bytes, label: str = "Tester"):
    ser.write(frame)
    print(f"  {label} TX: {frame.hex(' ').upper()}")

def _rx(label: str = "ECU") -> bytes:
    data = ser.read(8)
    if not data:
        raise TimeoutError("No response from ECU (timeout)")
    print(f"  {label} RX: {data.hex(' ').upper()}")
    return data


# ══════════════════════════════════════════════════════════════════════════════
#  ISO-TP send  (Tester → ECU)
#  Rule: if payload > 7 bytes, send FF, wait for ECU's FC, then send CFs.
# ══════════════════════════════════════════════════════════════════════════════

def isotp_send(payload: bytes):
    """Send a UDS payload using ISO-TP framing."""
    if len(payload) <= 7:
        _tx(_build_sf(payload))
        return

    # Multi-frame send here kansifto FF first frame
    _tx(_build_ff(payload))

    # Wait for ECU's FC Flow control
    fc = _rx()
    pci = fc[0] & 0xF0
    fs  = fc[0] & 0x0F
    if pci != 0x30:
        raise ValueError(f"Expected FC (0x3x), got 0x{fc[0]:02X}")
    if fs != 0x00:
        raise ValueError(f"FC FlowStatus not CTS: 0x{fs:02X}")

    # Send Consecutive Frames
    offset = 6
    sn = 1
    while offset < len(payload):
        chunk = payload[offset:offset + 7]
        _tx(_build_cf(chunk, sn))
        time.sleep(0.1)
        offset += 7
        sn = (sn + 1) & 0x0F

# ══════════════════════════════════════════════════════════════════════════════
#  ISO-TP receive  (ECU → Tester)
#  Rule: if ECU sends an FF, tester must respond with FC before CFs arrive.
# ══════════════════════════════════════════════════════════════════════════════

def isotp_recv() -> Optional[bytes]:
    """Receive a UDS response using ISO-TP framing. Returns reassembled payload."""
    first = _rx()
    pci = first[0] & 0xF0

    # ── Single Frame ──────────────────────────────────────────────────────────
    if pci == 0x00:
        length = first[0] & 0x0F
        if length == 0 or length > 7:
            print(f"  [WARN] SF with invalid length nibble: {length}")
            return None
        return bytes(first[1:1 + length])

    # ── First Frame (ECU response is multi-frame) ─────────────────────────────
    elif pci == 0x10:
        total_len = first[1]
        # Bytes [2..7] of the FF are the first 6 payload bytes
        payload = bytearray(first[2:8])

        # Tester must send FC to let ECU continue with CFs
        _tx(_build_fc())
        # Receive Consecutive Frames until we have total_len bytes
        while len(payload) < total_len:
            cf = _rx()
            if (cf[0] & 0xF0) != 0x20:
                print(f"  [ERR] Expected CF (0x2x), got 0x{cf[0]:02X}")
                return None
            remaining = total_len - len(payload)
            payload += cf[1:1 + min(7, remaining)]

        return bytes(payload)

    # ── Unexpected PCI ────────────────────────────────────────────────────────
    else:
        print(f"  [ERR] Unexpected PCI byte: 0x{first[0]:02X}")
        return None

# ══════════════════════════════════════════════════════════════════════════════
#  Input parser
# ══════════════════════════════════════════════════════════════════════════════

def parse_hex(cmd: str) -> bytes:
    """Parse a hex string like '1001' or '0x2704 9A07...' into bytes."""
    clean = cmd.strip().replace(" ", "").upper()
    if clean.startswith("0X"):
        clean = clean[2:]
    return bytes.fromhex(clean)

# ══════════════════════════════════════════════════════════════════════════════
#  Main loop
# ══════════════════════════════════════════════════════════════════════════════

def main():
    print("─" * 60)
    print("  SIGMA UDS Host — ISO-TP over UART")
    print("─" * 60)
    print("  Enter UDS payload as hex (spaces optional).")
    print("  Examples:")
    print("    1001                    → DiagnosticSession default")
    print("    1002                    → DiagnosticSession programming")
    print("    2701                    → SecurityAccess request seed")
    print("    2702 AA                 → SecurityAccess send key AA")
    print("    2703                    → HighSecurity AES request seed")
    print("    2704 <32-hex-chars>     → HighSecurity AES send key")
    print("    2200F0                  → ReadDID 0x00F0")
    print("─" * 60)
    print()

    while True:
        try:
            cmd = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n[EXIT]")
            break

        if not cmd:
            continue
        try:
            payload = parse_hex(cmd)
        except ValueError as e:
            print(f"  [ERR] Parse error: {e}\n")
            continue

        print(f"  Sending {len(payload)}-byte payload: {payload.hex(' ').upper()}")

        try:
            isotp_send(payload)
            response = isotp_recv()
        except TimeoutError as e:
            print(f"  [ERR] {e}\n")
            continue
        except ValueError as e:
            print(f"  [ERR] Protocol error: {e}\n")
            continue
        except serial.SerialException:
            print("  [ERR] Serial port lost\n")
            break

        if response:
            # Decode response type
            sid_resp = response[0] if response else 0
            if sid_resp == 0x7F:
                nrc = response[2] if len(response) > 2 else 0
                print(f"  → NRC 0x{nrc:02X} for SID 0x{response[1]:02X}")
            else:
                print(f"  → Positive response ({len(response)} bytes)")
            print(f"  Payload: {response.hex(' ').upper()}\n")
        else:
            print("  [WARN] No valid response received\n")

    ser.close()


if __name__ == "__main__":
    main()