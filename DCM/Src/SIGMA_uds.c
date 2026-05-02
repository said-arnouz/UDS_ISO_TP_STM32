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
static uint8_t assembled[21];
/* Security state — private to this file */
static uint16_t sec_seed      = 0;
static bool     sec_seed_sent = false;
static uint8_t  sec_attempts  = 0;
static bool     sec_locked    = false;
static uint32_t sec_timestamp = 0;
static bool     sec_unlocked  = false;

/* VIN number of the car */
static const uint8_t VIN_NUMBER[17] = {
    0x32, 0x54, 0x33, 0x52, 0x46, 0x52, 0x45, 0x56,
    0x37, 0x44, 0x57, 0x31, 0x30, 0x38, 0x31, 0x37, 0x37
};
/**
  * @brief Routes an outgoing UDS response over UART.
  *        payload <= 7 bytes transmits as SF directly.
  *        payload > 7 bytes delegates to SIGMA_ISO_TP_Send().
  */
void SIGMA_UART_Send(uint8_t *tx_buf, uint8_t len)
{
    uint8_t payload_len = tx_buf[0];

    if (payload_len <= ISO_TP_SF_MAX_PAYLOAD)
    {
        HAL_UART_Transmit(&huart2, tx_buf, 8u, 1000u);
    }
    else
    {
        SIGMA_ISO_TP_Send(&tx_buf[1], payload_len);
    }
}

/**
  * @brief Main UDS dispatcher.
  *        Decodes the incoming SF frame and routes to the correct handler.
  */
void SIGMA_UDS_Process(uint8_t *frame, uint8_t *tx_buf)
{
    uint8_t len        = frame[0];
    uint8_t sid        = frame[1];
    uint8_t sub        = frame[2];
    uint8_t data       = frame[3];
    uint8_t ctrl_param = frame[4];
    uint8_t value      = frame[5];

    memset(tx_buf, 0xAA, 8);

    /* Global length guard — SF payload must be 1 to 7 bytes */
    if (len < 1u || len > 7u)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_INCORRECT_MESSAGE_LENGTH;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

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
            /* sub 0x03 / 0x04 — AES high security */
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

/**
  * @brief SID 0x10 — DiagnosticSessionControl handler.
  */
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
            if (!(sec_unlocked || high_sec_unlocked))
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

/**
  * @brief SID 0x11 — ECUReset handler.
  */
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

/**
  * @brief SID 0x22 — ReadDataByIdentifier handler.
  */
void SIGMA_READ_DID(uint8_t length, uint16_t did, uint8_t *tx_buf)
{
    memset(tx_buf, 0xAA, 8);
    /* Even length that is not exactly 2 is malformed */
    if ((length % 2u) == 0u && length != 2u)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = SID_READ_DATA;
        tx_buf[3] = NRC_INCORRECT_MESSAGE_LENGTH;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    /* Odd length greater than 1 means multiple DIDs — response too long */
    if (length == 5u || length == 7u)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = SID_READ_DATA;
        tx_buf[3] = NRC_RESPONS_TO_LONG;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    /* length == 2 — valid single DID request */
    switch (did)
    {
        case DID_ECU_IDENTIFICATION_NUMBER :
        	/* VIN is : 2T3RFREV7DW108177 */

        	assembled[0] = 0x14;
        	assembled[1] = SID_READ_DATA + POS;   /* 0x62 */
        	assembled[2] = (uint8_t)(did >> 8);
        	assembled[3] = (uint8_t)(did & 0xFF);
            memcpy(&assembled[4], VIN_NUMBER, 17u);
            SIGMA_UART_Send(assembled, 21);
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
            /* Returns current session: DEFAULT=0x01 / PROGRAMMING=0x02 */
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

/**
  * @brief SID 0x31 — RoutineControl handler.
  */
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

/**
  * @brief SID 0x27 — SecurityAccess handler (sub 0x01 / 0x02 only).
  */
void SIGMA_SecurityAccess(uint8_t length, uint8_t sub, uint8_t key, uint8_t *tx_buf)
{
    memset(tx_buf, 0xAA, 8);

    /* Extra data bytes on REQUEST_SEED are not allowed */
    if (length > 2u && sub == REQUEST_SEED)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = SID_SECURITY;
        tx_buf[3] = NRC_INCORRECT_MESSAGE_LENGTH;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

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

        tx_buf[0] = 0x04;
        tx_buf[1] = SID_SECURITY + POS;   /* 0x67 */
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

        /* Expected key = seed_H XOR seed_L */
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

        /* Key correct — unlock level 1 security */
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
