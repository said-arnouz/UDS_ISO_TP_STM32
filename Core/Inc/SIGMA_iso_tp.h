/**
 * @file    SIGMA_iso_tp.h
 * @brief   ISO 15765-2 (ISO-TP) transport layer over UART (8-byte frames).
 *          Handles SID 0x27 sub 0x03 / 0x04 (AES Security Access).
 *
 * Frame layout (UART, 8 bytes):
 *   SF  [0x0N][SID][SUB][d0..d4][0xAA]       N = payload length (1-7)
 *   FF  [0x10][LEN][SID][SUB][d0][d1][d2][d3]
 *   CF  [0x2N][d0][d1][d2][d3][d4][d5][d6]   N = sequence number 1..F
 *   FC  [0x30][0x00][0x00][0xAA..]            BS=0 STmin=0
 *
 * @author  ARNOUZ SAID
 * @date    2026
 */

#ifndef SIGMA_ISO_TP_H
#define SIGMA_ISO_TP_H

#include "main.h"
#include <string.h>
#include <stdbool.h>
#include "crypto.h"

/* Maximum payload bytes in a Single Frame */
#define ISO_TP_SF_MAX_PAYLOAD       7u

/* Response payload length for AES seed/key (SID + SUB + 16 data bytes) */
#define KEY_RESPONS_LENGTH          18u

/* PCI type nibbles */
#define ISO_PCI_SF                  0x00u
#define ISO_PCI_FF                  0x10u
#define ISO_PCI_CF                  0x20u
#define ISO_PCI_FC                  0x30u

/* Flow Control constants */
#define ISO_FC_CTS                  0x00u   /* ContinueToSend */
#define ISO_FC_BS                   0x00u   /* BlockSize = 0  */
#define ISO_FC_STMIN                0x00u   /* STmin = 0 ms   */

/* Fixed UART frame size in bytes */
#define ISO_UART_FRAME_LEN          8u

/* Maximum reassembled payload size in bytes */
#define ISO_TP_BUF_SIZE             64u

/* Timeout between consecutive frames in milliseconds */
#define ISO_TP_CF_TIMEOUT_MS        60000u

/* Timeout waiting for Flow Control in milliseconds */
#define ISO_TP_FC_TIMEOUT_MS        60000u

/**
 * @brief ISO-TP reassembly state machine states.
 */
typedef enum
{
    ISO_TP_IDLE      = 0,   /* No active reassembly                          */
    ISO_TP_RECEIVING = 1,   /* FF received, FC sent, waiting for CFs         */
    ISO_TP_COMPLETE  = 2    /* All bytes received and reassembled            */
} IsoTp_State_t;

/**
 * @brief ISO-TP reassembly context.
 *        Holds the state of an ongoing multi-frame reception.
 */
typedef struct
{
    IsoTp_State_t state;
    uint8_t       buf[ISO_TP_BUF_SIZE];   /* reassembled payload buffer      */
    uint8_t       total_len;              /* expected payload length from FF  */
    uint8_t       received;              /* bytes received so far            */
    uint8_t       sn_expected;           /* next expected CF sequence number */
    uint32_t      timestamp;             /* HAL_GetTick() at last frame      */
} IsoTp_Ctx_t;

/**
 * @brief  ISO-TP entry point — call from main loop when PCI is FF or CF.
 * @details Routes assembled payload to the correct service handler by SID.
 * @param  frame  : 8-byte raw UART frame.
 * @param  tx_buf : 8-byte transmit scratch buffer.
 */
void SIGMA_ISO_TP_Process(uint8_t *frame, uint8_t *tx_buf);

/**
 * @brief  Sends a payload longer than 7 bytes using FF + FC wait + CFs.
 * @details Blocks internally while waiting for the tester's FC frame via
 *          the HAL_UARTEx_ReceiveToIdle_IT interrupt flag.
 * @param  payload : pointer to the full response payload (not tx_buf).
 * @param  len     : total payload length in bytes (must be greater than 7).
 */
void SIGMA_ISO_TP_Send(uint8_t *payload, uint8_t len);

/**
 * @brief  AES Security Access handler (SID 0x27 sub 0x03 / 0x04).
 * @details Called from two paths:
 *          - SF path (sub 0x03 REQUEST_AES): frame is the raw 8-byte UART frame.
 *          - Multi-frame path (sub 0x04 SEND_AES): frame is the assembled buffer
 *            with layout [total_len][SID][SUB][key0..key15].
 * @param  len    : payload length.
 * @param  sub    : REQUEST_AES (0x03) or SEND_AES (0x04).
 * @param  frame  : frame or assembled buffer depending on call path.
 * @param  tx_buf : 8-byte transmit scratch buffer.
 */
void SIGMA_HighSecurity(uint8_t len, uint8_t sub,
                        uint8_t *frame, uint8_t *tx_buf);

#endif /* SIGMA_ISO_TP_H */
