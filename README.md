# UDS ISO-TP STM32

A full **UDS (ISO 14229)** diagnostic stack implemented on **STM32F411RE** over UART,
paired with a **PyQt5 diagnostic host** that replicates a professional ECU tester.

![SIGMA DiagBox](images/1.PNG)

---

## Architecture

```
┌─────────────────────┐        UART 115200        ┌─────────────────────┐
│   SIGMA DiagBox     │ ◄────────────────────────► │   STM32F411RE ECU   │
│   (PyQt5 Host)      │     ISO-TP / UDS frames    │   (Firmware in C)   │
└─────────────────────┘                            └─────────────────────┘
```

![DCM Core Code](images/2.PNG)

---

## UDS Services

| SID  | Service                        | Notes                                                                        |
|------|--------------------------------|------------------------------------------------------------------------------|
| 0x10 | DiagnosticSessionControl       | Default / Programming session                                                |
| 0x11 | ECUReset                       | SW / HW / KeyOffOn reset                                                     |
| 0x22 | ReadDataByIdentifier           | Serial number, HW/SW version, session                                        |
| 0x27 | SecurityAccess                 | XOR seed/key + AES-ECB-128 high security                                     |
| 0x31 | RoutineControl                 | Erase memory, integrity check                                                |
| 0x2F | InputOutputControlByIdentifier | LED (real HW — PA5), Buzzer PWM, Fan PWM, Relay (3 simulated in UI) |

---

## ISO-TP Transport (ISO 15765-2)

| Frame | PCI    | Description                      |
|-------|--------|----------------------------------|
| SF    | `0x0N` | Single Frame (payload ≤ 7 bytes) |
| FF    | `0x10` | First Frame (payload > 7 bytes)  |
| CF    | `0x2N` | Consecutive Frame (sequence N)   |
| FC    | `0x30` | Flow Control (ContinueToSend)    |

Multi-frame transport is used for SID 0x27 sub 0x03/0x04 — the AES seed (18 bytes) and key exchange.

---

## Security Access — Two Levels

**Level 1 (0x27 0x01 / 0x02)**
- 2-byte seed from SysTick
- Key = `seed_H XOR seed_L`

**Level 2 — High Security (0x27 0x03 / 0x04)**
- 16-byte seed from SysTick
- Key = `AES_ECB_Encrypt(master_key, seed)`
- Transported via ISO-TP multi-frame
- Lockout after 3 failed attempts

---

## Hardware

> ⚠️ **Note:** `0x2F` controls **4 outputs** — **LED (PA5) is real HW on STM32**.
> Fan (TIM2 CH2), Buzzer (TIM3 CH1) and Relay (PB0) are **simulated in the PyQt5 UI only**.

![I/O Control UI](images/3.PNG)

| Component | Detail                        |
|-----------|-------------------------------|
| Board     | STM32F411RE Nucleo            |
| Interface | UART2 @ 115200 baud (ST-Link) |
| LED       | PA5 — Real HW              |
| Fan       | TIM2 CH2 — PWM 0–100% (UI sim)|
| Buzzer    | TIM3 CH1 — PWM 0–100% (UI sim)|
| Relay     | PB0 (UI sim)                  |

---

## Requirements

```bash
pip install pyserial PyQt5 cryptography
```

---

## Author

**ARNOUZ SAID** — Embedded Systems Engineer