/**
 ******************************************************************************
 * @file    SIGMA_dcm_core.h
 * @brief   Diagnostic Communication Manager – frame dispatcher and ECU state.
 *
 * @details This module sits between the BSP layer (main.c) and the service
 *          handlers (SIGMA_uds, SIGMA_iso_tp, SIGMA_io_control). It exposes
 *          the shared ECU runtime globals and decides which service handles
 *          each incoming ISO-UART frame based on the PCI nibble.
 *
 *          Layer mapping:
 *          main.c           ->  MCAL / BSP  (clock, GPIO, UART init)
 *          SIGMA_dcm_core   ->  DCM         (dispatch, ECU state)
 *          SIGMA_uds        ->  DSP / SID handlers
 *          SIGMA_iso_tp     ->  TP layer
 *          SIGMA_io_control ->  IOC service
 ******************************************************************************
 */
#ifndef SIGMA_DCM_CORE_H
#define SIGMA_DCM_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "SIGMA_uds.h"
#include "SIGMA_iso_tp.h"

/**
 * @brief  Process one received ISO-UART frame.
 * @details Decodes the PCI nibble of frame[0] and routes the frame to the
 *          correct service handler:
 *            SF (0x0X) -> SIGMA_UDS_Process
 *            FF / CF   -> SIGMA_ISO_TP_Process
 *            FC        -> ignored (handled inside SIGMA_ISO_TP_Send)
 *            unknown   -> General Reject NRC
 * @param  frame   Pointer to the raw received byte array (ISO_UART_FRAME_LEN).
 * @param  tx_buf  Transmit buffer passed through to service handlers.
 */
void DCM_Core_ProcessFrame(uint8_t *frame, uint8_t *tx_buf);

/**
 * @brief  Periodic tick — call every main-loop iteration.
 * @details Syncs s_ecu snapshot with the live globals, then delegates to
 *          SIGMA_IOControl_Tick() and any other services that need a regular
 *          update on every cycle.
 */
void DCM_Core_Tick(void);

#endif /* SIGMA_DCM_CORE_H */
