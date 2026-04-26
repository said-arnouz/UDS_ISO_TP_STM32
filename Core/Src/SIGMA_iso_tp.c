/**
 * @file    SIGMA_iso_tp.c
 * @brief   ISO 15765-2 transport layer over UART (8-byte frames).
 *
 * ── Frame layout (8 bytes) ────────────────────────────────────────────────
 *
 *   SF  [0x0N][SID][SUB][d0..d4][0xAA]     N = payload length (1-7)
 *   FF  [0x10][LEN][SID][SUB][d0][d1][d2][d3]
 *   CF  [0x2N][d0][d1][d2][d3][d4][d5][d6]  N = sequence number
 *   FC  [0x30][0x00][0x00][0xAA..]
 *
 * ── Receive path (Tester → STM32) ────────────────────────────────────────
 *
 *   FF [10][LEN][SID][SUB][d0..d3]  → store, send FC, wait CFs
 *   CF [2N][d...]                   → append until complete → dispatch by SID
 *
 * ── Send path (STM32 → Tester) ───────────────────────────────────────────
 *
 *   payload_len <= 7  → SF  (single 8-byte UART frame)
 *   payload_len >  7  → SIGMA_ISO_TP_Send() → FF + wait FC + CFs
 *
 * ── FIX NOTES ────────────────────────────────────────────────────────────
 *
 *   BUG FIXED: SIGMA_HighSecurity was passing &tx_buf[1] (only 7 bytes
 *   remaining) to SIGMA_ISO_TP_Send with len=18, causing an 11-byte
 *   buffer overread and sending garbage seed bytes to the tester.
 *   FIX: SIGMA_HighSecurity now builds the full response in its own
 *   local 18-byte array and passes that directly to SIGMA_ISO_TP_Send.
 *
 * @author  ARNOUZ SAID
 * @date    2026
 */

#include "SIGMA_iso_tp.h"
#include "SIGMA_uds.h"

/* ── Shared AES key (must match Python tester) ───────────────────────────── */
static const uint8_t aes_key[16] =
{
    0x01,0x02,0x03,0x04,
    0x05,0x06,0x07,0x08,
    0x09,0x0A,0x0B,0x0C,
    0x0D,0x0E,0x0F,0x10
};

/* ── AES security state ──────────────────────────────────────────────────── */
static uint8_t  aes_seed[16]      = {0};
static bool     aes_seed_sent     = false;
static uint8_t  aes_attempts      = 0;
static bool     aes_locked        = false;
static uint32_t aes_timestamp     = 0;
bool            high_sec_unlocked = false;

/* ── ISO-TP reassembly context ───────────────────────────────────────────── */
static IsoTp_Ctx_t iso_ctx = {0};

/* ═══════════════════════════════════════════════════════════════════════════
 *  Internal helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief  Sends one 8-byte ISO-TP FC frame (ContinueToSend).
 */
static void send_fc(void)
{
    uint8_t fc[ISO_UART_FRAME_LEN];
    fc[0] = (uint8_t)(ISO_PCI_FC | ISO_FC_CTS);  /* 0x30 */
    fc[1] = ISO_FC_BS;                            /* 0x00 */
    fc[2] = ISO_FC_STMIN;                         /* 0x00 */
    memset(&fc[3], 0xAAu, 5u);
    HAL_UART_Transmit(&huart2, fc, ISO_UART_FRAME_LEN, 1000u);
}

/**
 * @brief  Resets the reassembly context to IDLE.
 */
static void reset_ctx(void)
{
    memset(&iso_ctx, 0, sizeof(IsoTp_Ctx_t));
    iso_ctx.state = ISO_TP_IDLE;
}

/**
 * @brief  Sends NRC over 8-byte UART frame.
 */
static void send_nrc(uint8_t sid, uint8_t nrc_code, uint8_t *tx_buf)
{
    memset(tx_buf, 0xAAu, ISO_UART_FRAME_LEN);
    tx_buf[0] = 0x03u;
    tx_buf[1] = NRC;
    tx_buf[2] = sid;
    tx_buf[3] = nrc_code;
    SIGMA_UART_Send(tx_buf, ISO_UART_FRAME_LEN);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  SIGMA_ISO_TP_Send
 *
 *  Sends a payload > 7 bytes using FF → wait for tester FC → CFs.
 *
 *  The tester's FC arrives via HAL_UARTEx_ReceiveToIdle_IT interrupt which
 *  sets frame_ready and writes into frame[]. We poll frame_ready here
 *  instead of blocking with HAL_UART_Receive() to avoid competing with
 *  the interrupt.
 *
 *  @param  payload  pointer to the full response payload (NOT tx_buf).
 *  @param  len      total payload length in bytes (must be > 7).
 * ═══════════════════════════════════════════════════════════════════════════ */
void SIGMA_ISO_TP_Send(uint8_t *payload, uint8_t len)
{
    extern volatile uint8_t frame_ready;
    extern uint8_t          frame[];

    uint8_t tx_frame[ISO_UART_FRAME_LEN];
    uint8_t offset = 0u;
    uint8_t sn     = 1u;

    /* ── Send FF ──────────────────────────────────────────────────────────── */
    memset(tx_frame, 0xAAu, ISO_UART_FRAME_LEN);
    tx_frame[0] = ISO_PCI_FF;     /* 0x10                                     */
    tx_frame[1] = len;            /* total payload length                      */
    tx_frame[2] = payload[0];    /* payload byte 0                            */
    tx_frame[3] = payload[1];    /* payload byte 1                            */
    tx_frame[4] = payload[2];    /* payload byte 2                            */
    tx_frame[5] = payload[3];    /* payload byte 3                            */
    tx_frame[6] = payload[4];    /* payload byte 4                            */
    tx_frame[7] = payload[5];    /* payload byte 5                            */
    offset = 6u;                  /* 6 payload bytes already sent in FF        */

    HAL_UART_Transmit(&huart2, tx_frame, ISO_UART_FRAME_LEN, 1000u);

    /* ── Wait for tester's FC (via UART interrupt) ────────────────────────── */
    uint32_t t0 = HAL_GetTick();
    while (!frame_ready)
    {
        if ((HAL_GetTick() - t0) > ISO_TP_CF_TIMEOUT_MS)
            return;    /* FC timeout — abort silently                          */
    }
    frame_ready = 0;   /* consume the interrupt flag                          */

    /* Validate FC frame */
    if ((frame[0] & 0xF0u) != ISO_PCI_FC)
        return;        /* not a FC — abort                                     */
    if ((frame[0] & 0x0Fu) != ISO_FC_CTS)
        return;        /* not ContinueToSend — abort                          */

    /* ── Send Consecutive Frames ──────────────────────────────────────────── */
    while (offset < len)
    {
        memset(tx_frame, 0xAAu, ISO_UART_FRAME_LEN);
        tx_frame[0] = (uint8_t)(ISO_PCI_CF | (sn & 0x0Fu));   /* 0x21, 0x22… */

        for (uint8_t i = 1u; i < ISO_UART_FRAME_LEN && offset < len; i++, offset++)
            tx_frame[i] = payload[offset];

        HAL_UART_Transmit(&huart2, tx_frame, ISO_UART_FRAME_LEN, 1000u);
        sn = (sn + 1u) & 0x0Fu;    /* wrap 0xF → 0x0                         */
    }
}
/* ═══════════════════════════════════════════════════════════════════════════
 *  SIGMA_ISO_TP_Process
 *
 *  Called from main() when decode_pci() returns PCI_FF or PCI_CF.
 *  Reassembles multi-frame messages then dispatches by SID.
 * ═══════════════════════════════════════════════════════════════════════════ */
void SIGMA_ISO_TP_Process(uint8_t *frame, uint8_t *tx_buf)
{
    uint8_t pci_type = (uint8_t)(frame[0] & 0xF0u);

    /* ── Timeout guard on open reassembly ────────────────────────────────── */
    if (iso_ctx.state == ISO_TP_RECEIVING)
    {
        if ((HAL_GetTick() - iso_ctx.timestamp) > ISO_TP_FC_TIMEOUT_MS)
        {
            uint8_t sid = iso_ctx.buf[0];
            reset_ctx();
            send_nrc(sid, NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED, tx_buf);
            return;
        }
    }

    switch (pci_type)
    {
        /* ══ FF — First Frame ════════════════════════════════════════════════
         *  frame[0] = 0x10
         *  frame[1] = total payload length
         *  frame[2] = SID
         *  frame[3] = SUB
         *  frame[4..7] = first 4 data bytes
         * ═══════════════════════════════════════════════════════════════════ */
        case ISO_PCI_FF:
        {
            reset_ctx();

            uint8_t total = frame[1];
            uint8_t sid   = frame[2];

            if (total == 0u || total > ISO_TP_BUF_SIZE)
            {
                send_nrc(sid, NRC_INCORRECT_MESSAGE_LENGTH, tx_buf);
                return;
            }

            /* buf layout: [SID][SUB][d0][d1][d2][d3][d4...] */
            iso_ctx.buf[0] = sid;        /* SID         */
            iso_ctx.buf[1] = frame[3];   /* SUB         */
            iso_ctx.buf[2] = frame[4];   /* data byte 0 */
            iso_ctx.buf[3] = frame[5];   /* data byte 1 */
            iso_ctx.buf[4] = frame[6];   /* data byte 2 */
            iso_ctx.buf[5] = frame[7];   /* data byte 3 */

            iso_ctx.total_len   = total;
            iso_ctx.received    = 6u;    /* SID+SUB+4 bytes stored             */
            iso_ctx.sn_expected = 1u;
            iso_ctx.state       = ISO_TP_RECEIVING;
            iso_ctx.timestamp   = HAL_GetTick();

            send_fc();
            break;
        }

        /* ══ CF — Consecutive Frame ══════════════════════════════════════════
         *  frame[0] = 0x2N  (N = sequence number)
         *  frame[1..7] = up to 7 data bytes
         * ═══════════════════════════════════════════════════════════════════ */
        case ISO_PCI_CF:
        {
            if (iso_ctx.state != ISO_TP_RECEIVING)
            {
                send_nrc(SID_SECURITY, NRC_REQUEST_SEQUENCE_ERROR, tx_buf);
                return;
            }

            uint8_t sn = (uint8_t)(frame[0] & 0x0Fu);

            if (sn != iso_ctx.sn_expected)
            {
                uint8_t sid = iso_ctx.buf[0];
                reset_ctx();
                send_nrc(sid, NRC_REQUEST_SEQUENCE_ERROR, tx_buf);
                return;
            }

            for (uint8_t i = 1u; i < ISO_UART_FRAME_LEN; i++)
            {
                if (iso_ctx.received >= iso_ctx.total_len)
                    break;
                iso_ctx.buf[iso_ctx.received++] = frame[i];
            }

            iso_ctx.sn_expected = (uint8_t)((iso_ctx.sn_expected + 1u) & 0x0Fu);
            iso_ctx.timestamp   = HAL_GetTick();

            /* ── Reassembly complete? ─────────────────────────────────────── */
            if (iso_ctx.received >= iso_ctx.total_len)
            {
                iso_ctx.state = ISO_TP_COMPLETE;

                /*
                 * assembled layout:
                 *   [0]      = total_len
                 *   [1]      = SID
                 *   [2]      = SUB
                 *   [3..N]   = data bytes
                 */
                uint8_t assembled[ISO_TP_BUF_SIZE + 2u];
                assembled[0] = iso_ctx.total_len;
                memcpy(&assembled[1], iso_ctx.buf, iso_ctx.total_len);

                uint8_t sid = iso_ctx.buf[0];
                uint8_t sub = iso_ctx.buf[1];

                if (sid == SID_SECURITY)
                {
                    SIGMA_HighSecurity(iso_ctx.total_len, sub, assembled, tx_buf);
                }
                else
                {
                    send_nrc(sid, NRC_SERVICE_NOT_SUPPORTED, tx_buf);
                }

                reset_ctx();
            }
            break;
        }

        /* ── FC from tester: consumed by SIGMA_ISO_TP_Send, ignore here ───── */
        case ISO_PCI_FC:
            break;

        /* ── SF arriving here is a routing error from main.c ─────────────── */
        case ISO_PCI_SF:
        default:
            send_nrc(frame[1], NRC_INCORRECT_MESSAGE_LENGTH, tx_buf);
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  SIGMA_HighSecurity  —  SID 0x27 sub 0x03 / 0x04  (AES security access)
 *
 *  Called from two paths:
 *    1. SIGMA_UDS_Process (SF path)  for sub 0x03 (REQUEST_AES)
 *       frame = raw 8-byte UART frame
 *       assembled[0] is frame[0] = SF length byte
 *
 *    2. SIGMA_ISO_TP_Process (multi-frame path)  for sub 0x04 (SEND_AES)
 *       frame = assembled[] buffer:
 *         frame[0] = total_len
 *         frame[1] = SID
 *         frame[2] = SUB
 *         frame[3..18] = 16-byte received key
 *
 *  ── CRITICAL FIX ──────────────────────────────────────────────────────────
 *  Previously, the REQUEST_AES response was built as:
 *      tx_buf[0] = 18;
 *      memcpy(&tx_buf[1], resp, 7);      ← only 7 bytes copied
 *      SIGMA_ISO_TP_Send(&tx_buf[1], 18) ← reads 18 bytes from a 7-byte window!
 *
 *  tx_buf is only 8 bytes, so bytes [8..18] were a stack/heap overread,
 *  sending 11 garbage bytes as part of the seed to the tester.
 *
 *  FIX: Use a dedicated local resp[18] buffer and pass it directly to
 *  SIGMA_ISO_TP_Send(). tx_buf is only used for SF NRC responses.
 * ═══════════════════════════════════════════════════════════════════════════ */
void SIGMA_HighSecurity(uint8_t len, uint8_t sub, uint8_t *frame, uint8_t *tx_buf)
{
    memset(tx_buf, 0xAAu, ISO_UART_FRAME_LEN);

    /* ── Locked? ──────────────────────────────────────────────────────────── */
    if (aes_locked)
    {
        send_nrc(SID_SECURITY, NRC_EXCEEDED_NUMBERS_OF_ATTEMPTS, tx_buf);
        return;
    }

    /* ══ REQUEST_AES (sub 0x03) ══════════════════════════════════════════════
     *  Request  : SF  [02][27][03]
     *  Response : FF+CFs  total 18 bytes: [67][03][seed0..seed15]
     * ═══════════════════════════════════════════════════════════════════════ */
    if (sub == REQUEST_AES)
    {
        if (len != 2u)
        {
            send_nrc(SID_SECURITY, NRC_INCORRECT_MESSAGE_LENGTH, tx_buf);
            return;
        }

        /* Generate 16-byte seed from SysTick */
        uint32_t t = HAL_GetTick();
        for (uint8_t i = 0u; i < 16u; i++)
            aes_seed[i] = (uint8_t)((t >> ((i % 4u) * 8u)) ^ (i * 0x5Au));

        aes_seed_sent = true;
        aes_timestamp = HAL_GetTick();
        aes_attempts  = 0u;

        /*
         * Build the full 18-byte response payload in its own buffer.
         * Layout: [67][03][seed0][seed1]...[seed15]
         *
         * This buffer is passed directly to SIGMA_ISO_TP_Send().
         * Do NOT use tx_buf here — tx_buf is only 8 bytes and would
         * be overread by SIGMA_ISO_TP_Send reading 18 bytes from it.
         */
        uint8_t resp[18u];
        resp[0] = (uint8_t)(SID_SECURITY + POS);   /* 0x67 */
        resp[1] = REQUEST_AES;                      /* 0x03 */
        memcpy(&resp[2], aes_seed, 16u);            /* seed bytes 0..15 */

        SIGMA_ISO_TP_Send(resp, 18u);
    }

    /* ══ SEND_AES (sub 0x04) ═════════════════════════════════════════════════
     *  frame[0] = total_len (18)
     *  frame[1] = SID (0x27)
     *  frame[2] = SUB (0x04)
     *  frame[3..18] = received 16-byte key from tester
     * ═══════════════════════════════════════════════════════════════════════ */
    else if (sub == SEND_AES)
    {
        /* Sequence guard */
        if (!aes_seed_sent)
        {
            send_nrc(SID_SECURITY, NRC_REQUEST_SEQUENCE_ERROR, tx_buf);
            return;
        }

        /* Timeout guard (60 seconds) */
        if ((HAL_GetTick() - aes_timestamp) > SEC_TIMEOUT_MS)
        {
            aes_seed_sent = false;
            send_nrc(SID_SECURITY, NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED, tx_buf);
            return;
        }

        /* Extract received key from assembled frame: frame[3..18] */
        uint8_t received_key[16u] = {0};
        memcpy(received_key, &frame[3], 16u);

        /* Compute expected key = AES-128-ECB-Encrypt(aes_key, aes_seed) */
        uint8_t  expected_key[16u] = {0};
        int32_t  out_len           = 0;
        AESECBctx_stt ctx;
        ctx.mKeySize   = CRL_AES128_KEY;
        ctx.mFlags     = E_SK_DEFAULT;
        ctx.mIvSize    = 0;
        ctx.mContextId = 0;

        if (AES_ECB_Encrypt_Init(&ctx, aes_key, NULL) != AES_SUCCESS)
        {
            send_nrc(SID_SECURITY, NRC_GENERAL_REJECT, tx_buf);
            return;
        }
        if (AES_ECB_Encrypt_Append(&ctx,
                                    aes_seed,     16u,
                                    expected_key, &out_len) != AES_SUCCESS)
        {
            send_nrc(SID_SECURITY, NRC_GENERAL_REJECT, tx_buf);
            return;
        }
        AES_ECB_Encrypt_Finish(&ctx, NULL, &out_len);

        /* Compare received vs expected */
        if (memcmp(received_key, expected_key, 16u) != 0)
        {
            aes_attempts++;
            if (aes_attempts >= SEC_MAX_ATTEMPTS)
            {
                aes_locked    = true;
                aes_seed_sent = false;
                send_nrc(SID_SECURITY, NRC_EXCEEDED_NUMBERS_OF_ATTEMPTS, tx_buf);
            }
            else
            {
                send_nrc(SID_SECURITY, NRC_INVALID_KEY, tx_buf);
            }
            return;
        }

        /* Key correct — unlock high security */
        high_sec_unlocked = true;
        aes_seed_sent     = false;
        aes_attempts      = 0u;

        tx_buf[0] = 0x02u;
        tx_buf[1] = (uint8_t)(SID_SECURITY + POS);   /* 0x67 */
        tx_buf[2] = SEND_AES;                         /* 0x04 */
        SIGMA_UART_Send(tx_buf, ISO_UART_FRAME_LEN);
    }

    /* ── Unknown sub-function ─────────────────────────────────────────────── */
    else
    {
        send_nrc(SID_SECURITY, NRC_SUBFUNCTION_NOT_SUPPORTED, tx_buf);
    }
}
