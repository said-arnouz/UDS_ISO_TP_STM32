/**
 * @file    SIGMA_io_control.h
 * @brief   Public API for UDS SID 0x2F – InputOutputControlByIdentifier
 * @author  ARNOUZ SAID
 * @date    2026
 */

#ifndef SIGMA_IO_CONTROL_H
#define SIGMA_IO_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx_hal.h"   /* Adjust to your MCU family */

/*Service identifier*/
#define SID_IO_CONTROL              0x2Fu

/*IOC signal IDs*/
#define IOC_ID_LED                  0x0001u  /* PA5  onboard LED              */
#define IOC_ID_BUZZER               0x0002u  /* TIM3 CH1 buzzer               */
#define IOC_ID_FAN                  0x0003u  /* TIM2 CH2 cooling fan          */
#define IOC_ID_RELAY                0x0004u  /* PB0  power relay              */

/*Control parameter bytes (ISO 14229-1 §11.3.2)*/
#define IO_CTRL_RETURN_TO_ECU       0x00u   /* Release — ECU resumes control  */
#define IO_CTRL_RESET_TO_DEFAULT    0x01u   /* Force to NVM default value     */
#define IO_CTRL_FREEZE_CURRENT      0x02u   /* Lock at current live value     */
#define IO_CTRL_SHORT_TERM_ADJUST   0x03u   /* Override with tester value     */

/**
 * @brief  Main IOControl handler – call from SIGMA_UDS_Process when SID==0x2F.
 * @param  len        frame[0]  – payload byte count (4 or 5).
 * @param  ioc_id     16-bit signal ID  (frame[2]<<8 | frame[3]).
 * @param  ctrl_param control action    (frame[4]).
 * @param  value      override value    (frame[5]; only used with SHORT_TERM_ADJUST).
 * @param  sid        0x2F – used in NRC frames.
 * @param  tx_buf     8-byte TX buffer.
 */
void SIGMA_IOControl(uint8_t  len,
                     uint16_t ioc_id,
                     uint8_t  ctrl_param,
                     uint8_t  value,
                     uint8_t  sid,
                     uint8_t *tx_buf);

/**
 * @brief  Periodic tick – restores autonomous ECU behaviour when overrides
 *         are released.  Call from main loop or 10 ms timer task.
 */
void SIGMA_IOControl_Tick(void);

#endif /* SIGMA_IO_CONTROL_H */
