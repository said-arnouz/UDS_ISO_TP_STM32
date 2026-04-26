/**
 * @file    SIGMA_uds.h
 * @brief   UDS (Unified Diagnostic Services) layer for STM32.
 *          Provides DiagnosticSessionControl, ECUReset, SecurityAccess,
 *          and ReadDataByIdentifier services over UART (ISO 14229).
 * @author  ARNOUZ SAID
 * @date    2026
 */

#ifndef SIGMA_UDS_H
#define SIGMA_UDS_H


#include <SIGMA_io_control.h>
#include "stdbool.h"
#include "string.h"
#include "main.h"

/*
 * Security timing
 */
#define SEC_TIMEOUT_MS                      120000U
#define SEC_MAX_ATTEMPTS                    3
/*
 * Application Start Address
 */
#define APP_ADDRESS                      	0x08020000UL
/*
 * Positive response offset
 */
#define POS                                 0x40
/*
 * NRC — Negative Response Codes
 */
#define NRC                                 0x7F
#define NRC_GENERAL_REJECT                  0x10
#define NRC_SERVICE_NOT_SUPPORTED           0x11
#define NRC_SUBFUNCTION_NOT_SUPPORTED       0x12
#define NRC_INCORRECT_MESSAGE_LENGTH        0x13
#define NRC_RESPONS_TO_LONG                 0x14
#define NRC_CONDITION_NOT_CORRECT           0x22
#define NRC_REQUEST_SEQUENCE_ERROR          0x24
#define NRC_REQUEST_OUT_OF_RANG             0x31
#define NRC_SECURITY_ACCESS_DENIED          0x33
#define NRC_INVALID_KEY                     0x35
#define NRC_EXCEEDED_NUMBERS_OF_ATTEMPTS    0x36
#define NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED 0x37
#define NRC_GENERAL_PRGRAMMING_FAILURE		0x72

/*
 * SID 0x10 — DiagnosticSessionControl
 */
#define SID_DIAG_SESSION                    0x10
#define DEFAULT_SESSION                     0x01
#define PROGRAMMING_SESSION                 0x02

/*
 * SID 0x11 — ECUReset
 */
#define SID_RESET                           0x11
#define HW_RESET                            0x01
#define KEY_OFF_ON_RESET                    0x02
#define SW_RESET                            0x03

/*
 * SID 0x27 — SecurityAccess
 */
#define SID_SECURITY                        0x27
#define REQUEST_SEED                        0x01
#define SEND_KEY                            0x02
#define REQUEST_AES                        	0x03
#define SEND_AES                            0x04

/*
 * SID 0x22 — ReadDataByIdentifier (DIDs)
 */
#define SID_READ_DATA                       0x22
#define DID_ECU_SERIAL_NUMBER               0xF18C
#define DID_ECU_HW_VERSION                  0xF193
#define DID_ECU_SW_VERSION                  0xF195
#define DID_ECU_SESSION                     0xF186

/*
 * SID 0x31 — RoutineControl
 */
#define SID_ROUTINE_CONTROL                 0x31
#define START_ROUTINE                 		0x01
#define STOP_ROUTINE	                  	0x02 // maghadich nakhdam bih
#define ROUTINE_ERASE_MEMORY               	0xFF00
#define ROUTINE_CHECK_INTEGRITY				0xFF01

/*
 * External variables (defined in main.c)
 */
extern UART_HandleTypeDef  huart2;
extern uint8_t             Ecu_session;
extern uint8_t             speed;
extern uint8_t             temp;
extern bool                flag;

/*Function prototypes*/
/**
 * @brief  Sends a UDS frame over UART.
 * @param  tx_buf : pointer to the 8-byte frame buffer.
 * @param  len    : number of bytes to transmit.
 */
void SIGMA_UART_Send(uint8_t *tx_buf, uint8_t len);

/**
 * @brief  Main UDS dispatcher — call once per while(1) iteration.
 *         Reads one 8-byte frame, decodes SID and routes to the
 *         appropriate service handler.
 * @param  frame  : pointer to the 8-byte receive buffer.
 * @param  tx_buf : pointer to the 8-byte transmit buffer.
 */
void SIGMA_UDS_Process(uint8_t *frame, uint8_t *tx_buf);

/**
 * @brief  SID 0x10 — DiagnosticSessionControl handler.
 * @param  len    : payload length (must be 2).
 * @param  sub    : requested session (DEFAULT_SESSION / PROGRAMMING_SESSION).
 * @param  sid    : service ID (for NRC frame).
 * @param  tx_buf : pointer to the transmit buffer.
 */
void SIGMA_DiagSession(uint8_t len, uint8_t sub, uint8_t sid, uint8_t *tx_buf);

/**
 * @brief  SID 0x11 — ECUReset handler.
 * @details Only allowed in PROGRAMMING_SESSION.
 *          HW_RESET and KEY_OFF_ON_RESET require flag == true.
 *          SW_RESET always allowed in programming session.
 * @param  len    : payload length (must be 2).
 * @param  sub    : reset type (HW_RESET / KEY_OFF_ON_RESET / SW_RESET).
 * @param  sid    : service ID (for NRC frame).
 * @param  tx_buf : pointer to the transmit buffer.
 */
void SIGMA_ECUReset(uint8_t len, uint8_t sub, uint8_t sid, uint8_t *tx_buf);

/**
 * @brief  SID 0x27 — SecurityAccess handler (seed/key exchange).
 * @details
 *          Step 1 : sub = REQUEST_SEED → ECU sends [03][67][seed_H][seed_L].
 *          Step 2 : sub = SEND_KEY     → ECU checks key = seed_H ^ seed_L.
 *          - 3 wrong keys  → EXCEEDED_NUMBERS_OF_ATTEMPTS, locked until reset.
 *          - Timeout > 60s → REQUIRED_TIME_DELAY_NOT_EXPIRED.
 * @param  length : payload length from frame[0].
 * @param  sub    : REQUEST_SEED (0x01) or SEND_KEY (0x02).
 * @param  key    : key byte sent by tester (used only for SEND_KEY).
 * @param  tx_buf : pointer to the transmit buffer.
 */
void SIGMA_READ_DID(uint8_t length, uint16_t did, uint8_t *tx_buf);

/**
 * @brief  SID 0x31 — RoutineControl handler.
 * @param  len    : payload length.
 * @param  sub    : START(0x01) / STOP(0x02) / REQUEST_RESULTS(0x03).
 * @param  rid_H  : Routine ID high byte.
 * @param  rid_L  : Routine ID low byte.
 * @param  sid    : service ID for NRC.
 * @param  tx_buf : transmit buffer.
 */
void SIGMA_RoutineControl(uint8_t len, uint8_t sub,
                          uint8_t rid_H, uint8_t rid_L,
                          uint8_t sid, uint8_t *tx_buf);
/**
 * @brief  SID 0x22 — ReadDataByIdentifier handler.
 * @details Supported DIDs: ECU_SERIAL_NUMBER, HW_VERSION, SW_VERSION, SESSION.
 *          Rejects frames with even length (malformed) or multiple DIDs.
 *          Requires temp <= 10 (checked in dispatcher).
 * @param  length : payload length from frame[0].
 * @param  did    : 16-bit DID built as (frame[2] << 8) | frame[3].
 * @param  tx_buf : pointer to the transmit buffer.
 */
void SIGMA_SecurityAccess(uint8_t length, uint8_t sub, uint8_t key, uint8_t *tx_buf);

#endif /* SIGMA_UDS_H */
