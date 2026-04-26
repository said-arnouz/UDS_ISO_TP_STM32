/**
 * @file    SIGMA_io_control.h
 * @brief   Public API for UDS SID 0x2F — InputOutputControlByIdentifier.
 *          Controls 4 ECU signals: LED, Buzzer, Fan, Relay.
 * @author  ARNOUZ SAID
 * @date    2026
 */

#ifndef SIGMA_IO_CONTROL_H
#define SIGMA_IO_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx_hal.h"

/* Service identifier */
#define SID_IO_CONTROL              0x2Fu

/* IOC signal identifiers */
#define IOC_ID_LED                  0x0001u   /* PA5  onboard LED    */
#define IOC_ID_BUZZER               0x0002u   /* TIM3 CH1 buzzer     */
#define IOC_ID_FAN                  0x0003u   /* TIM2 CH2 fan        */
#define IOC_ID_RELAY                0x0004u   /* PB0  power relay    */

/* Control parameter bytes (ISO 14229-1 section 11.3.2) */
#define IO_CTRL_RETURN_TO_ECU       0x00u   /* Release — ECU resumes control */
#define IO_CTRL_RESET_TO_DEFAULT    0x01u   /* Force to NVM default value    */
#define IO_CTRL_FREEZE_CURRENT      0x02u   /* Lock at current live value    */
#define IO_CTRL_SHORT_TERM_ADJUST   0x03u   /* Override with tester value    */

/**
 * @brief  SID 0x2F — InputOutputControlByIdentifier main handler.
 * @details Dispatches to the correct signal handler based on ioc_id.
 *          Requires PROGRAMMING_SESSION and high_sec_unlocked.
 * @param  len        : frame[0] payload byte count (must be 4 or 5).
 * @param  ioc_id     : 16-bit signal identifier (frame[2]<<8 | frame[3]).
 * @param  ctrl_param : control action byte (frame[4]).
 * @param  value      : override value (frame[5], only used with SHORT_TERM_ADJUST).
 * @param  sid        : service ID 0x2F for NRC frames.
 * @param  tx_buf     : 8-byte transmit buffer.
 */
void SIGMA_IOControl(uint8_t  len,
                     uint16_t ioc_id,
                     uint8_t  ctrl_param,
                     uint8_t  value,
                     uint8_t  sid,
                     uint8_t *tx_buf);

/**
 * @brief  Periodic IOControl tick — call from main loop or 10 ms timer.
 * @details When an override flag is clear the ECU resumes autonomous control.
 *          LED blinks at 1 Hz, fan ramps with temperature, buzzer stays silent.
 */
void SIGMA_IOControl_Tick(void);

#endif /* SIGMA_IO_CONTROL_H */
