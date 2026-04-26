/**
 * @file    SIGMA_uds.c
 * @brief   UDS (Unified Diagnostic Services) implementation for STM32.
 *          Handles UART frame reception and dispatches to service handlers:
 *            - SID 0x10 : DiagnosticSessionControl
 *            - SID 0x11 : ECUReset
 *            - SID 0x22 : ReadDataByIdentifier
 *            - SID 0x27 : SecurityAccess (seed/key, AES high-security)
 *            - SID 0x2F : InputOutputControlByIdentifier
 *            - SID 0x31 : RoutineControl
 * @author  ARNOUZ SAID
 * @date    2026
 */

#include "SIGMA_uds.h"
#include "SIGMA_flash.h"
#include "SIGMA_iso_tp.h"

extern bool high_sec_unlocked;

/* ── Security state — private to this file ───────────────────────────────── */
static uint16_t sec_seed      = 0;
static bool     sec_seed_sent = false;
static uint8_t  sec_attempts  = 0;
static bool     sec_locked    = false;
static uint32_t sec_timestamp = 0;
static bool     sec_unlocked  = false;

/* ═══════════════════════════════════════════════════════════════════════════
 *  SIGMA_UART_Send
 *
 *  Routes an outgoing UDS response:
 *    - tx_buf[0] <= 7  → SF: transmit tx_buf as-is (8-byte UART frame)
 *    - tx_buf[0] >  7  → multi-frame: caller is responsible for building
 *                         a dedicated payload buffer and calling
 *                         SIGMA_ISO_TP_Send() directly.
 *
 *  NOTE: Do NOT call this with tx_buf[0] > 7 unless the full payload is
 *  already packed correctly in tx_buf[1..]. For multi-frame responses,
 *  use a local payload buffer and call SIGMA_ISO_TP_Send() directly
 *  (see SIGMA_HighSecurity in SIGMA_iso_tp.c for the correct pattern).
 * ═══════════════════════════════════════════════════════════════════════════ */
void SIGMA_UART_Send(uint8_t *tx_buf, uint8_t len)
{
    uint8_t payload_len = tx_buf[0];

    if (payload_len <= ISO_TP_SF_MAX_PAYLOAD)
    {
        /* SF — transmit the 8-byte frame directly */
        HAL_UART_Transmit(&huart2, tx_buf, 8u, 1000u);
    }
    else
    {
        /*
         * Multi-frame: payload starts at tx_buf[1], length is tx_buf[0].
         * The caller must ensure tx_buf has at least (payload_len + 1) bytes.
         * For standard 8-byte tx_buf, only use this path when payload_len <= 7.
         */
        SIGMA_ISO_TP_Send(&tx_buf[1], payload_len);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  SIGMA_UDS_Process
 *
 *  Main UDS dispatcher — reads one SF frame and routes to handler.
 *
 *  Frame layout (SF, received from tester):
 *    frame[0] = LEN   (payload byte count, must be 1-7 for SF)
 *    frame[1] = SID
 *    frame[2] = SUB   (sub-function or DID_H)
 *    frame[3] = DATA  (key or DID_L, 0x00 if unused)
 *    frame[4] = CTRL_PARAM
 *    frame[5] = VALUE
 * ═══════════════════════════════════════════════════════════════════════════ */
void SIGMA_UDS_Process(uint8_t *frame, uint8_t *tx_buf)
{
    uint8_t len        = frame[0];
    uint8_t sid        = frame[1];
    uint8_t sub        = frame[2];
    uint8_t data       = frame[3];
    uint8_t ctrl_param = frame[4];
    uint8_t value      = frame[5];

    memset(tx_buf, 0xAA, 8);

    /* Global length guard */
    if (len < 1u || len > 7u)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_INCORRECT_MESSAGE_LENGTH;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    /* Route to service handler */
    if (sid == SID_DIAG_SESSION)
    {
        SIGMA_DiagSession(len, sub, sid, tx_buf);
    }
    else if (sid == SID_RESET)
    {
        SIGMA_ECUReset(len, sub, sid, tx_buf);
    }
    else if (sid == SID_SECURITY)
    {
        if (sub == REQUEST_SEED || sub == SEND_KEY)
        {
            SIGMA_SecurityAccess(len, sub, data, tx_buf);
        }
        else
        {
            /* sub 0x03 / 0x04 — AES high security, called from SF path */
            SIGMA_HighSecurity(len, sub, frame, tx_buf);
        }
    }
    else if (sid == SID_READ_DATA)
    {
        if (temp > 10)
        {
            tx_buf[0] = 0x03;
            tx_buf[1] = NRC;
            tx_buf[2] = sid;
            tx_buf[3] = NRC_CONDITION_NOT_CORRECT;
            SIGMA_UART_Send(tx_buf, 8);
            return;
        }
        uint16_t did = ((uint16_t)sub << 8) | data;
        SIGMA_READ_DID(len, did, tx_buf);
    }
    else if (sid == SID_ROUTINE_CONTROL)
    {
        SIGMA_RoutineControl(len, sub, data, frame[4], sid, tx_buf);
    }
    else if (sid == SID_IO_CONTROL)
    {
        uint16_t ioc_id = ((uint16_t)sub << 8) | data;
        SIGMA_IOControl(len, ioc_id, ctrl_param, value, sid, tx_buf);
    }
    else
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_SERVICE_NOT_SUPPORTED;
        SIGMA_UART_Send(tx_buf, 8);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  SIGMA_DiagSession — SID 0x10
 *
 *  Switches ECU between Default and Programming sessions.
 *  Programming session requires sec_unlocked == true and speed == 0.
 * ═══════════════════════════════════════════════════════════════════════════ */
void SIGMA_DiagSession(uint8_t len, uint8_t sub, uint8_t sid, uint8_t *tx_buf)
{
    if (len != 2u)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_INCORRECT_MESSAGE_LENGTH;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    switch (sub)
    {
        case DEFAULT_SESSION:
            Ecu_session = DEFAULT_SESSION;
            tx_buf[0]   = 0x02;
            tx_buf[1]   = SID_DIAG_SESSION + POS;   /* 0x50 */
            tx_buf[2]   = DEFAULT_SESSION;
            SIGMA_UART_Send(tx_buf, 8);
            break;

        case PROGRAMMING_SESSION:
            if (!sec_unlocked)
            {
                tx_buf[0] = 0x03;
                tx_buf[1] = NRC;
                tx_buf[2] = sid;
                tx_buf[3] = NRC_SECURITY_ACCESS_DENIED;
                SIGMA_UART_Send(tx_buf, 8);
                break;
            }
            if (speed != 0)
            {
                tx_buf[0] = 0x03;
                tx_buf[1] = NRC;
                tx_buf[2] = sid;
                tx_buf[3] = NRC_CONDITION_NOT_CORRECT;
                SIGMA_UART_Send(tx_buf, 8);
                break;
            }
            Ecu_session = PROGRAMMING_SESSION;
            tx_buf[0]   = 0x02;
            tx_buf[1]   = SID_DIAG_SESSION + POS;   /* 0x50 */
            tx_buf[2]   = PROGRAMMING_SESSION;
            SIGMA_UART_Send(tx_buf, 8);
            break;

        default:
            tx_buf[0] = 0x03;
            tx_buf[1] = NRC;
            tx_buf[2] = sid;
            tx_buf[3] = NRC_SUBFUNCTION_NOT_SUPPORTED;
            SIGMA_UART_Send(tx_buf, 8);
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  SIGMA_ECUReset — SID 0x11
 *
 *  Only accessible in PROGRAMMING_SESSION.
 *  HW_RESET and KEY_OFF_ON_RESET require flag == true.
 * ═══════════════════════════════════════════════════════════════════════════ */
void SIGMA_ECUReset(uint8_t len, uint8_t sub, uint8_t sid, uint8_t *tx_buf)
{
    if (Ecu_session != PROGRAMMING_SESSION)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_SERVICE_NOT_SUPPORTED;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    if (len > 2u)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_INCORRECT_MESSAGE_LENGTH;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    switch (sub)
    {
        case SW_RESET:
            tx_buf[0] = 0x02;
            tx_buf[1] = SID_RESET + POS;   /* 0x51 */
            tx_buf[2] = SW_RESET;
            SIGMA_UART_Send(tx_buf, 8);
            NVIC_SystemReset();
            break;

        case KEY_OFF_ON_RESET:
        case HW_RESET:
            if (flag == true)
            {
                tx_buf[0] = 0x02;
                tx_buf[1] = SID_RESET + POS;
                tx_buf[2] = sub;
                SIGMA_UART_Send(tx_buf, 8);
                NVIC_SystemReset();
            }
            else
            {
                tx_buf[0] = 0x03;
                tx_buf[1] = NRC;
                tx_buf[2] = sid;
                tx_buf[3] = NRC_CONDITION_NOT_CORRECT;
                SIGMA_UART_Send(tx_buf, 8);
            }
            break;

        default:
            tx_buf[0] = 0x03;
            tx_buf[1] = NRC;
            tx_buf[2] = sid;
            tx_buf[3] = NRC_SUBFUNCTION_NOT_SUPPORTED;
            SIGMA_UART_Send(tx_buf, 8);
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  SIGMA_READ_DID — SID 0x22
 *
 *  Length rules:
 *    Even length (0,2,4,6) with len==2 → valid single DID
 *    Even length != 2      → NRC_INCORRECT_MESSAGE_LENGTH (malformed)
 *    Odd length (1,3,5,7)  → NRC_RESPONS_TO_LONG (multiple DIDs)
 *
 *  Supported DIDs:
 *    DID_ECU_SERIAL_NUMBER : 4 data bytes  → total response len 0x07
 *    DID_ECU_HW_VERSION    : 1 data byte   → total response len 0x04
 *    DID_ECU_SW_VERSION    : 1 data byte   → total response len 0x04
 *    DID_ECU_SESSION       : 1 data byte   → total response len 0x04
 * ═══════════════════════════════════════════════════════════════════════════ */
void SIGMA_READ_DID(uint8_t length, uint16_t did, uint8_t *tx_buf)
{
    memset(tx_buf, 0xAA, 8);

    /* Even length that isn't exactly 2 → malformed */
    if ((length % 2u) == 0u && length != 2u)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = SID_READ_DATA;
        tx_buf[3] = NRC_INCORRECT_MESSAGE_LENGTH;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    /* Odd length > 1 → multiple DIDs, response would be too long */
    if (length == 5u || length == 7u)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = SID_READ_DATA;
        tx_buf[3] = NRC_RESPONS_TO_LONG;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    /* length == 2 → valid single DID request */
    switch (did)
    {
        case DID_ECU_SERIAL_NUMBER:
            /* SN0001 → 4 bytes: 'S' 'N' 0x00 0x01 */
            tx_buf[0] = 0x07;
            tx_buf[1] = SID_READ_DATA + POS;   /* 0x62 */
            tx_buf[2] = (uint8_t)(did >> 8);
            tx_buf[3] = (uint8_t)(did & 0xFF);
            tx_buf[4] = 0x53;   /* 'S' */
            tx_buf[5] = 0x4E;   /* 'N' */
            tx_buf[6] = 0x00;
            tx_buf[7] = 0x01;
            SIGMA_UART_Send(tx_buf, 8);
            break;

        case DID_ECU_HW_VERSION:
            /* v1.0 encoded as 0x10 */
            tx_buf[0] = 0x04;
            tx_buf[1] = SID_READ_DATA + POS;
            tx_buf[2] = (uint8_t)(did >> 8);
            tx_buf[3] = (uint8_t)(did & 0xFF);
            tx_buf[4] = 0x10;
            SIGMA_UART_Send(tx_buf, 8);
            break;

        case DID_ECU_SW_VERSION:
            /* v2.1 encoded as 0x21 */
            tx_buf[0] = 0x04;
            tx_buf[1] = SID_READ_DATA + POS;
            tx_buf[2] = (uint8_t)(did >> 8);
            tx_buf[3] = (uint8_t)(did & 0xFF);
            tx_buf[4] = 0x21;
            SIGMA_UART_Send(tx_buf, 8);
            break;

        case DID_ECU_SESSION:
            tx_buf[0] = 0x04;
            tx_buf[1] = SID_READ_DATA + POS;
            tx_buf[2] = (uint8_t)(did >> 8);
            tx_buf[3] = (uint8_t)(did & 0xFF);
            tx_buf[4] = Ecu_session;
            SIGMA_UART_Send(tx_buf, 8);
            break;

        default:
            tx_buf[0] = 0x03;
            tx_buf[1] = NRC;
            tx_buf[2] = SID_READ_DATA;
            tx_buf[3] = NRC_REQUEST_OUT_OF_RANG;
            SIGMA_UART_Send(tx_buf, 8);
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  SIGMA_RoutineControl — SID 0x31
 *
 *  Only accessible in PROGRAMMING_SESSION with high_sec_unlocked.
 *  Frame: [LEN][31][SUB][RID_H][RID_L]   len must be 4.
 * ═══════════════════════════════════════════════════════════════════════════ */
void SIGMA_RoutineControl(uint8_t len, uint8_t sub,
                          uint8_t rid_H, uint8_t rid_L,
                          uint8_t sid, uint8_t *tx_buf)
{
    memset(tx_buf, 0xAA, 8);

    if (len != 4u)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_INCORRECT_MESSAGE_LENGTH;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    if (Ecu_session != PROGRAMMING_SESSION)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_SERVICE_NOT_SUPPORTED;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    if (!high_sec_unlocked)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_SECURITY_ACCESS_DENIED;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    if (sub != START_ROUTINE)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_SUBFUNCTION_NOT_SUPPORTED;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    uint16_t rid = ((uint16_t)rid_H << 8) | rid_L;

    switch (rid)
    {
        case ROUTINE_ERASE_MEMORY:
            if (SIGMA_Flash_Erase(APP_ADDRESS) != FLASH_OK)
            {
                tx_buf[0] = 0x03;
                tx_buf[1] = NRC;
                tx_buf[2] = SID_ROUTINE_CONTROL;
                tx_buf[3] = NRC_GENERAL_PRGRAMMING_FAILURE;
                SIGMA_UART_Send(tx_buf, 8);
            }
            else
            {
                tx_buf[0] = 0x04;
                tx_buf[1] = SID_ROUTINE_CONTROL + POS;   /* 0x71 */
                tx_buf[2] = START_ROUTINE;
                tx_buf[3] = rid_H;
                tx_buf[4] = rid_L;
                SIGMA_UART_Send(tx_buf, 8);
            }
            break;

        case ROUTINE_CHECK_INTEGRITY:
            tx_buf[0] = 0x04;
            tx_buf[1] = SID_ROUTINE_CONTROL + POS;
            tx_buf[2] = START_ROUTINE;
            tx_buf[3] = rid_H;
            tx_buf[4] = rid_L;
            SIGMA_UART_Send(tx_buf, 8);
            break;

        default:
            tx_buf[0] = 0x03;
            tx_buf[1] = NRC;
            tx_buf[2] = sid;
            tx_buf[3] = NRC_REQUEST_OUT_OF_RANG;
            SIGMA_UART_Send(tx_buf, 8);
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  SIGMA_SecurityAccess — SID 0x27 (sub 0x01 / 0x02 only)
 *
 *  Key algorithm: key = seed_H XOR seed_L
 *  State machine:
 *    sec_locked    → always NRC_EXCEEDED_NUMBERS_OF_ATTEMPTS
 *    REQUEST_SEED  → generate seed, start timer, reset attempts
 *    SEND_KEY      → verify sequence, timeout, key correctness
 * ═══════════════════════════════════════════════════════════════════════════ */
void SIGMA_SecurityAccess(uint8_t length, uint8_t sub, uint8_t key, uint8_t *tx_buf)
{
    memset(tx_buf, 0xAA, 8);

    /* Extra data bytes on REQUEST_SEED → NRC */
    if (length > 2u && sub == REQUEST_SEED)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = SID_SECURITY;
        tx_buf[3] = NRC_INCORRECT_MESSAGE_LENGTH;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    /* Locked state */
    if (sec_locked)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = SID_SECURITY;
        tx_buf[3] = NRC_EXCEEDED_NUMBERS_OF_ATTEMPTS;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    if (sub == REQUEST_SEED)
    {
        sec_seed      = (uint16_t)(HAL_GetTick() & 0xFFFF);
        sec_seed_sent = true;
        sec_timestamp = HAL_GetTick();
        sec_attempts  = 0;

        uint8_t seed_H = (uint8_t)(sec_seed >> 8);
        uint8_t seed_L = (uint8_t)(sec_seed & 0xFF);

        tx_buf[0] = 0x04;               /* LEN: SID_RESP + SUB + seed_H + seed_L */
        tx_buf[1] = SID_SECURITY + POS; /* 0x67 */
        tx_buf[2] = REQUEST_SEED;
        tx_buf[3] = seed_H;
        tx_buf[4] = seed_L;
        SIGMA_UART_Send(tx_buf, 8);
    }
    else if (sub == SEND_KEY)
    {
        if (!sec_seed_sent)
        {
            tx_buf[0] = 0x03;
            tx_buf[1] = NRC;
            tx_buf[2] = SID_SECURITY;
            tx_buf[3] = NRC_REQUEST_SEQUENCE_ERROR;
            SIGMA_UART_Send(tx_buf, 8);
            return;
        }

        if ((HAL_GetTick() - sec_timestamp) > SEC_TIMEOUT_MS)
        {
            sec_seed_sent = false;
            tx_buf[0]     = 0x03;
            tx_buf[1]     = NRC;
            tx_buf[2]     = SID_SECURITY;
            tx_buf[3]     = NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED;
            SIGMA_UART_Send(tx_buf, 8);
            return;
        }

        /* Expected key: seed_H XOR seed_L */
        uint8_t expected = (uint8_t)(sec_seed >> 8) ^ (uint8_t)(sec_seed & 0xFF);

        if (key != expected)
        {
            sec_attempts++;
            if (sec_attempts >= SEC_MAX_ATTEMPTS)
            {
                sec_locked    = true;
                sec_seed_sent = false;
                tx_buf[3]     = NRC_EXCEEDED_NUMBERS_OF_ATTEMPTS;
            }
            else
            {
                tx_buf[3] = NRC_INVALID_KEY;
            }
            tx_buf[0] = 0x03;
            tx_buf[1] = NRC;
            tx_buf[2] = SID_SECURITY;
            SIGMA_UART_Send(tx_buf, 8);
            return;
        }

        /* Key correct */
        sec_unlocked  = true;
        sec_seed_sent = false;
        sec_attempts  = 0;

        tx_buf[0] = 0x02;
        tx_buf[1] = SID_SECURITY + POS;   /* 0x67 */
        tx_buf[2] = SEND_KEY;
        SIGMA_UART_Send(tx_buf, 8);
    }
    else
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = SID_SECURITY;
        tx_buf[3] = NRC_SUBFUNCTION_NOT_SUPPORTED;
        SIGMA_UART_Send(tx_buf, 8);
    }
}
