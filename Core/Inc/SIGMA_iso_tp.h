/**
 * @file    SIGMA_iso_tp.h
 * @brief   ISO 15765-2 (ISO-TP) layer — strict SF/FF/CF/FC framing.
 *          Handles only SID 0x27 sub 0x03 / 0x04 (AES Security Access).
 *          All UART frames are fixed 8 bytes.
 *
 * Frame layout (UART, 8 bytes):
 *
 *   FF  [10][LEN][SID][SUB][d0][d1][d2][d3]   LEN = total payload length
 *   CF  [2N][d0][d1][d2][d3][d4][d5][d6]       N  = sequence number 1..F
 *   FC  [30][00][00][AA][AA][AA][AA][AA]         BS=0 STmin=0
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

#define ISO_TP_SF_MAX_PAYLOAD   7u   /* max bytes in a single frame */
#define KEY_RESPONS_LENGTH			18u   /* bytes of key */
/* ── PCI nibbles ──────────────────────────────────────────────────────────── */
#define ISO_PCI_SF          0x00u   /* Single Frame        */
#define ISO_PCI_FF          0x10u   /* First Frame         */
#define ISO_PCI_CF          0x20u   /* Consecutive Frame   */
#define ISO_PCI_FC          0x30u   /* Flow Control        */

/* ── FC constants ─────────────────────────────────────────────────────────── */
#define ISO_FC_CTS          0x00u   /* ContinueToSend      */
#define ISO_FC_BS           0x00u   /* BlockSize = 0       */
#define ISO_FC_STMIN        0x00u   /* STmin     = 0 ms    */

/* ── Frame geometry ───────────────────────────────────────────────────────── */
#define ISO_UART_FRAME_LEN  8u      /* fixed UART frame size                  */
#define ISO_FF_DATA_OFFSET  4u      /* FF: SID+SUB occupy [2][3], data at [4] */
#define ISO_FF_DATA_BYTES   4u      /* bytes carried in FF (bytes [4..7])     */
#define ISO_CF_DATA_BYTES   7u      /* bytes carried in each CF (bytes [1..7])*/

/* ── Reassembly buffer ────────────────────────────────────────────────────── */
#define ISO_TP_BUF_SIZE     64u     /* max reassembled payload (bytes)        */

/* ── Timeout ──────────────────────────────────────────────────────────────── */
#define ISO_TP_CF_TIMEOUT_MS	60000u /* max wait between FF→CF or CF→CF        */
#define ISO_TP_FC_TIMEOUT_MS	60000u
/* ── State machine ────────────────────────────────────────────────────────── */
typedef enum
{
    ISO_TP_IDLE       = 0,
    ISO_TP_RECEIVING  = 1,   /* FF received, FC sent, waiting for CFs */
    ISO_TP_COMPLETE   = 2    /* all bytes reassembled                  */
} IsoTp_State_t;

/* ── Context ──────────────────────────────────────────────────────────────── */
typedef struct
{
    IsoTp_State_t state;
    uint8_t       buf[ISO_TP_BUF_SIZE];   /* reassembled payload              */
    uint8_t       total_len;              /* expected payload length from FF   */
    uint8_t       received;              /* bytes received so far             */
    uint8_t       sn_expected;           /* next expected CF sequence number  */
    uint32_t      timestamp;             /* HAL_GetTick() at last frame       */
} IsoTp_Ctx_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * @brief  Entry point — called from main when frame[0] high nibble is FF/CF.
 * @param  frame  : 8-byte raw UART frame.
 * @param  tx_buf : 8-byte transmit scratch buffer.
 */
void SIGMA_ISO_TP_Process(uint8_t *frame, uint8_t *tx_buf);

/**
 * @brief  Sends a payload > 7 bytes using FF + FC-wait + CFs.
 * @param  payload : pointer to data to send.
 * @param  len     : payload length in bytes.
 */
void SIGMA_ISO_TP_Send(uint8_t *payload, uint8_t len);

/**
 * @brief  AES Security Access handler (called after full reassembly).
 * @param  len    : reassembled payload length.
 * @param  sub    : sub-function (REQUEST_AES / SEND_AES).
 * @param  frame  : reassembled payload buffer.
 * @param  tx_buf : transmit scratch buffer.
 */
void SIGMA_HighSecurity(uint8_t len, uint8_t sub,
                        uint8_t *frame, uint8_t *tx_buf);

#endif /* SIGMA_ISO_TP_H */
