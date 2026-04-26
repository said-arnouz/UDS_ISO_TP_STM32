/**
 * @file    SIGMA_flash.h
 * @brief   Flash memory abstraction layer for STM32F411RE.
 * @author  ARNOUZ SAID
 * @date    2026
 */

#ifndef INC_SIGMA_FLASH_H_
#define INC_SIGMA_FLASH_H_

typedef enum
{
	FLASH_ERROR,
	FLASH_OK
} FLASH_Status_t;
/**
 * @brief  Erases all flash sectors from start_address to end of flash.
 * @details Unlocks flash, performs sector erase from the sector containing
 *          start_address up to SECTOR_7 (inclusive), then re-locks flash.
 *          Typically called before writing new firmware in bootloader context.
 * @param  start_address : first address to erase (sector-aligned recommended).
 * @return FLASH_OK    : erase successful.
 *         FLASH_ERROR : HAL erase failed (sector_error indicates failing sector).
 */
FLASH_Status_t SIGMA_Flash_Erase(uint32_t start_address);

/**
 * @brief  Writes a byte array to flash memory using 32-bit word programming.
 * @details HAL_FLASH_Program requires word-aligned writes.
 *          Bytes are packed little-endian into 32-bit words.
 *          If length is not a multiple of 4, the last word is zero-padded
 *          (remaining bits stay 0xFF from erase, partial write fills only used bytes).
 * @param  address : destination flash address (word-aligned recommended).
 * @param  data    : pointer to source data buffer.
 * @param  length  : number of bytes to write.
 * @return FLASH_OK    : all words written successfully.
 *         FLASH_ERROR : data is NULL or HAL program failed.
 */
FLASH_Status_t SIGMA_Flash_Write(uint32_t address, uint8_t *data, uint32_t length);

/**
 * @brief  Reads bytes from flash memory using direct memory-mapped access.
 * @details Flash is memory-mapped on STM32F4 — no HAL call needed, direct
 *          pointer dereference is sufficient and faster than HAL read APIs.
 * @param  address : source flash address to read from.
 * @param  data    : pointer to destination buffer.
 * @param  length  : number of bytes to read.
 * @return FLASH_OK always (read cannot fail on valid addresses).
 */
FLASH_Status_t SIGMA_Flash_Read(uint32_t address, uint8_t *data, uint32_t length);



#endif /* INC_SIGMA_FLASH_H_ */
