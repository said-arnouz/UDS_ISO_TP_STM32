/**
 ******************************************************************************
 * @file    SIGMA_dcm_core.c
 * @brief   Diagnostic Communication Manager – frame dispatcher and ECU state.
 ******************************************************************************
 */

#include <SIGMA_dcm_core.h>
#include "SIGMA_uds.h"
#include "SIGMA_iso_tp.h"
#include "main.h"

/* ISO 15765-2 PCI nibble values used to identify the frame type */
typedef enum
{
    PCI_SF      = 0x00,   /* Single Frame      — complete message in one frame  */
    PCI_FF      = 0x10,   /* First Frame       — start of a multi-frame message */
    PCI_CF      = 0x20,   /* Consecutive Frame — continuation of a FF           */
    PCI_FC      = 0x30,   /* Flow Control      — sent by receiver to pace CF TX */
    PCI_UNKNOWN = 0xFF
} PCI_Type_t;

/**
 * @brief  Decode the PCI type from the first byte of an ISO-UART frame.
 * @details Masks the upper nibble and maps it to the matching PCI_Type_t value.
 *          Returns PCI_UNKNOWN for any nibble not defined by ISO 15765-2.
 * @param  byte0  First byte of the received frame.
 * @return Decoded PCI_Type_t value.
 */
static PCI_Type_t prv_DecodePci(uint8_t byte0)
{
    switch (byte0 & 0xF0u)
    {
        case 0x00u: return PCI_SF;
        case 0x10u: return PCI_FF;
        case 0x20u: return PCI_CF;
        case 0x30u: return PCI_FC;
        default:    return PCI_UNKNOWN;
    }
}

/**
 * @brief  Send a General Reject NRC response for malformed or unknown frames.
 * @details Builds a 4-byte negative response using the service ID from frame[1]
 *          and transmits it immediately via SIGMA_UART_Send.
 * @param  frame   The offending received frame (frame[1] used as echo SID).
 * @param  tx_buf  Transmit buffer to build the response into.
 */
static void prv_SendGeneralReject(uint8_t *frame, uint8_t *tx_buf)
{
    tx_buf[0] = 0x03u;
    tx_buf[1] = NRC;
    tx_buf[2] = frame[1];
    tx_buf[3] = NRC_GENERAL_REJECT;
    SIGMA_UART_Send(tx_buf, 8u);
}
/**
 * @brief  Process one received ISO-UART frame.
 * @details Decodes the PCI nibble of frame[0] and routes the frame to the
 *          correct service handler:
 *            SF (0x0X) → SIGMA_UDS_Process
 *            FF / CF   → SIGMA_ISO_TP_Process
 *            FC        → ignored (handled inside SIGMA_ISO_TP_Send)
 *            unknown   → General Reject NRC
 * @param  frame   Pointer to the raw received byte array (ISO_UART_FRAME_LEN).
 * @param  tx_buf  Transmit buffer passed through to service handlers.
 */
void DCM_Core_ProcessFrame(uint8_t *frame, uint8_t *tx_buf)
{
    switch (prv_DecodePci(frame[0]))
    {
        case PCI_SF:
            SIGMA_UDS_Process(frame, tx_buf);
            break;

        case PCI_FF:
        case PCI_CF:
            SIGMA_ISO_TP_Process(frame, tx_buf);
            break;

        case PCI_FC:
            /* Flow Control from the tester — pacing is handled inside
               SIGMA_ISO_TP_Send, nothing to do at the dispatcher level */
            break;

        case PCI_UNKNOWN:
        default:
            prv_SendGeneralReject(frame, tx_buf);
            break;
    }
}

/**
 * @brief  Periodic tick — call every main-loop iteration.
 * @details Delegates to SIGMA_IOControl_Tick() and any other services
 *          that require a regular update on every cycle.
 */
void DCM_Core_Tick(void)
{
    SIGMA_IOControl_Tick();
}

