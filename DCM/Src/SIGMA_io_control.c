/**
 * @file    SIGMA_io_control.c
 * @brief   UDS SID 0x2F — InputOutputControlByIdentifier implementation.
 *          Controls 4 ECU signals:
 *            IOC_ID_LED    0x0001 — onboard LED   (GPIO  PA5)
 *            IOC_ID_BUZZER 0x0002 — buzzer         (PWM   TIM3_CH1)
 *            IOC_ID_FAN    0x0003 — cooling fan    (PWM   TIM2_CH2, 0-100%)
 *            IOC_ID_RELAY  0x0004 — power relay    (GPIO  PB0)
 *
 * Frame layout received from tester (8-byte UART frame):
 *   [0] LEN        = 4 or 5
 *   [1] SID        = 0x2F
 *   [2] IOC_ID_H
 *   [3] IOC_ID_L
 *   [4] CTRL_PARAM
 *   [5] VALUE      (only used with SHORT_TERM_ADJUST)
 *   [6..7] 0xAA padding
 *
 * Positive response (8 bytes):
 *   [0] 0x04
 *   [1] 0x6F  (0x2F + 0x40)
 *   [2] IOC_ID_H
 *   [3] IOC_ID_L
 *   [4] CTRL_PARAM echoed back
 *   [5..7] 0xAA padding
 *
 * @author  ARNOUZ SAID
 * @date    2026
 */

#include "SIGMA_uds.h"
#include "SIGMA_io_control.h"

extern bool high_sec_unlocked;

/* IOControl signal identifiers */
#define IOC_ID_LED      0x0001u
#define IOC_ID_BUZZER   0x0002u
#define IOC_ID_FAN      0x0003u
#define IOC_ID_RELAY    0x0004u

/* SID 0x2F positive response offset */
#define IOC_POS         0x40u

/* NVM defaults restored by RESET_TO_DEFAULT */
#define LED_DEFAULT     GPIO_PIN_RESET   /* LED off         */
#define BUZZER_DEFAULT  0u               /* 0% duty silent  */
#define FAN_DEFAULT     30u              /* 30% idle speed  */
#define RELAY_DEFAULT   GPIO_PIN_RESET   /* relay open      */

/* ECU override flags — true means tester has control */
static bool ioc_led_override    = false;
static bool ioc_buzzer_override = false;
static bool ioc_fan_override    = false;
static bool ioc_relay_override  = false;

/* Frozen values captured by FREEZE_CURRENT */
static GPIO_PinState ioc_led_frozen    = GPIO_PIN_RESET;
static uint8_t       ioc_buzzer_frozen = 0;
static uint8_t       ioc_fan_frozen    = 0;
static GPIO_PinState ioc_relay_frozen  = GPIO_PIN_RESET;

/**
  * @brief Reads the current LED state from the GPIO output data register.
  */
static GPIO_PinState _LED_ReadCurrent(void)
{
    return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_5);
}

/**
  * @brief Reads the current TIM3 CH1 CCR as a 0-100% duty value.
  */
static uint8_t _Buzzer_ReadCurrent(void)
{
    uint32_t arr = TIM3->ARR;
    if (arr == 0) return 0;
    return (uint8_t)((TIM3->CCR1 * 100u) / arr);
}

/**
  * @brief Reads the current TIM2 CH2 CCR as a 0-100% duty value.
  */
static uint8_t _Fan_ReadCurrent(void)
{
    uint32_t arr = TIM2->ARR;
    if (arr == 0) return 0;
    return (uint8_t)((TIM2->CCR2 * 100u) / arr);
}

/**
  * @brief Reads the current relay GPIO output state.
  */
static GPIO_PinState _Relay_ReadCurrent(void)
{
    return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0);
}

/**
  * @brief Sets TIM3 CH1 duty cycle for the buzzer. duty is clamped to 0-100.
  */
static void _Buzzer_SetDuty(uint8_t duty)
{
    if (duty > 100) duty = 100;
    TIM3->CCR1 = (TIM3->ARR * duty) / 100u;
}

/**
  * @brief Sets TIM2 CH2 duty cycle for the fan. duty is clamped to 0-100.
  */
static void _Fan_SetDuty(uint8_t duty)
{
    if (duty > 100) duty = 100;
    TIM2->CCR2 = (TIM2->ARR * duty) / 100u;
}

/**
  * @brief SID 0x2F — InputOutputControlByIdentifier main handler.
  */
void SIGMA_IOControl(uint8_t  len,
                     uint16_t ioc_id,
                     uint8_t  ctrl_param,
                     uint8_t  value,
                     uint8_t  sid,
                     uint8_t *tx_buf)
{
    memset(tx_buf, 0xAA, 8);

    /* Session guard — only allowed outside Default session */
    if (Ecu_session == DEFAULT_SESSION)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_SERVICE_NOT_SUPPORTED;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    /* High security must be unlocked */
    if (!high_sec_unlocked)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_SECURITY_ACCESS_DENIED;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    /* Length check — 4 bytes without value, 5 bytes with value */
    if (len < 4 || len > 5)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_INCORRECT_MESSAGE_LENGTH;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    /* SHORT_TERM_ADJUST requires the value byte so len must be 5 */
    if (ctrl_param == IO_CTRL_SHORT_TERM_ADJUST && len != 5)
    {
        tx_buf[0] = 0x03;
        tx_buf[1] = NRC;
        tx_buf[2] = sid;
        tx_buf[3] = NRC_INCORRECT_MESSAGE_LENGTH;
        SIGMA_UART_Send(tx_buf, 8);
        return;
    }

    switch (ioc_id)
    {
        case IOC_ID_LED:
            switch (ctrl_param)
            {
                case IO_CTRL_RETURN_TO_ECU:
                    ioc_led_override = false;
                    break;

                case IO_CTRL_RESET_TO_DEFAULT:
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, LED_DEFAULT);
                    ioc_led_override = true;
                    break;

                case IO_CTRL_FREEZE_CURRENT:
                    ioc_led_frozen   = _LED_ReadCurrent();
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, ioc_led_frozen);
                    ioc_led_override = true;
                    break;

                case IO_CTRL_SHORT_TERM_ADJUST:
                    /* value 0x00 LED OFF, anything else LED ON */
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5,
                                      (value == 0x00) ? GPIO_PIN_RESET : GPIO_PIN_SET);
                    ioc_led_override = true;
                    break;

                default:
                    tx_buf[0] = 0x03; tx_buf[1] = NRC;
                    tx_buf[2] = sid;  tx_buf[3] = NRC_SUBFUNCTION_NOT_SUPPORTED;
                    SIGMA_UART_Send(tx_buf, 8);
                    return;
            }
            break;

        case IOC_ID_BUZZER:
            switch (ctrl_param)
            {
                case IO_CTRL_RETURN_TO_ECU:
                    ioc_buzzer_override = false;
                    break;

                case IO_CTRL_RESET_TO_DEFAULT:
                    _Buzzer_SetDuty(BUZZER_DEFAULT);
                    ioc_buzzer_override = true;
                    break;

                case IO_CTRL_FREEZE_CURRENT:
                    ioc_buzzer_frozen   = _Buzzer_ReadCurrent();
                    _Buzzer_SetDuty(ioc_buzzer_frozen);
                    ioc_buzzer_override = true;
                    break;

                case IO_CTRL_SHORT_TERM_ADJUST:
                    /* value = 0-100% duty cycle */
                    _Buzzer_SetDuty(value);
                    ioc_buzzer_override = true;
                    break;

                default:
                    tx_buf[0] = 0x03; tx_buf[1] = NRC;
                    tx_buf[2] = sid;  tx_buf[3] = NRC_SUBFUNCTION_NOT_SUPPORTED;
                    SIGMA_UART_Send(tx_buf, 8);
                    return;
            }
            break;

        case IOC_ID_FAN:
            switch (ctrl_param)
            {
                case IO_CTRL_RETURN_TO_ECU:
                    ioc_fan_override = false;
                    break;

                case IO_CTRL_RESET_TO_DEFAULT:
                    _Fan_SetDuty(FAN_DEFAULT);
                    ioc_fan_override = true;
                    break;

                case IO_CTRL_FREEZE_CURRENT:
                    ioc_fan_frozen   = _Fan_ReadCurrent();
                    _Fan_SetDuty(ioc_fan_frozen);
                    ioc_fan_override = true;
                    break;

                case IO_CTRL_SHORT_TERM_ADJUST:
                    /* value = 0-100% fan speed */
                    _Fan_SetDuty(value);
                    ioc_fan_override = true;
                    break;

                default:
                    tx_buf[0] = 0x03; tx_buf[1] = NRC;
                    tx_buf[2] = sid;  tx_buf[3] = NRC_SUBFUNCTION_NOT_SUPPORTED;
                    SIGMA_UART_Send(tx_buf, 8);
                    return;
            }
            break;

        case IOC_ID_RELAY:
            switch (ctrl_param)
            {
                case IO_CTRL_RETURN_TO_ECU:
                    ioc_relay_override = false;
                    break;

                case IO_CTRL_RESET_TO_DEFAULT:
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, RELAY_DEFAULT);
                    ioc_relay_override = true;
                    break;

                case IO_CTRL_FREEZE_CURRENT:
                    ioc_relay_frozen   = _Relay_ReadCurrent();
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, ioc_relay_frozen);
                    ioc_relay_override = true;
                    break;

                case IO_CTRL_SHORT_TERM_ADJUST:
                    /* value 0x00 relay open, 0x01 relay closed */
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0,
                                      (value == 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
                    ioc_relay_override = true;
                    break;

                default:
                    tx_buf[0] = 0x03; tx_buf[1] = NRC;
                    tx_buf[2] = sid;  tx_buf[3] = NRC_SUBFUNCTION_NOT_SUPPORTED;
                    SIGMA_UART_Send(tx_buf, 8);
                    return;
            }
            break;

        default:
            tx_buf[0] = 0x03;
            tx_buf[1] = NRC;
            tx_buf[2] = sid;
            tx_buf[3] = NRC_REQUEST_OUT_OF_RANG;
            SIGMA_UART_Send(tx_buf, 8);
            return;
    }

    /* Positive response */
    tx_buf[0] = 0x04;
    tx_buf[1] = SID_IO_CONTROL + IOC_POS;     /* 0x6F */
    tx_buf[2] = (uint8_t)(ioc_id >> 8);
    tx_buf[3] = (uint8_t)(ioc_id & 0xFF);
    tx_buf[4] = ctrl_param;
    SIGMA_UART_Send(tx_buf, 8);
}

/**
  * @brief Periodic IOControl tick — restores ECU autonomous behaviour
  *        when override flags are cleared.
  */
void SIGMA_IOControl_Tick(void)
{
    /* LED blinks at 1 Hz when ECU has control */
    if (!ioc_led_override)
    {
        if ((HAL_GetTick() % 1000u) < 500u)
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
        else
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
    }

    /* Fan ramps with temperature when ECU has control */
    if (!ioc_fan_override)
    {
        extern uint8_t temp;
        uint8_t auto_duty = (temp > 80u) ? 100u :
                            (temp > 60u) ?  70u :
                            (temp > 40u) ?  50u : FAN_DEFAULT;
        _Fan_SetDuty(auto_duty);
    }

    /* Buzzer stays silent under ECU control */
    if (!ioc_buzzer_override)
    {
        _Buzzer_SetDuty(0);
    }

    /* Relay is managed by ECU fault logic when not overridden */
}
