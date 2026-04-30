/**
 * @file    SIGMA_flash.c
 * @brief   Flash memory abstraction layer for STM32F411RE.
 *          Provides sector erase, word-aligned write, and byte read
 *          over internal flash via HAL_FLASH driver.
 * @author  ARNOUZ SAID
 * @date    2026
 */

#include "string.h"
#include "stdio.h"
#include "stm32f4xx_hal.h"
#include "SIGMA_flash.h"

/**
 * @brief  Maps a flash address to its corresponding sector number.
 * @details STM32F411RE flash sector layout (1MB total):
 *            SECTOR_0 : 0x08000000 – 0x08003FFF  (16 KB)
 *            SECTOR_1 : 0x08004000 – 0x08007FFF  (16 KB)
 *            SECTOR_2 : 0x08008000 – 0x0800BFFF  (16 KB)
 *            SECTOR_3 : 0x0800C000 – 0x0800FFFF  (16 KB)
 *            SECTOR_4 : 0x08010000 – 0x0801FFFF  (64 KB)
 *            SECTOR_5 : 0x08020000 – 0x0803FFFF  (128 KB)
 *            SECTOR_6 : 0x08040000 – 0x0805FFFF  (128 KB)
 *            SECTOR_7 : 0x08060000 – 0x0807FFFF  (128 KB)
 * @param  address : flash address to map.
 * @return Sector number (FLASH_SECTOR_0 … FLASH_SECTOR_7).
 */
static uint32_t SIGMA_Flash_GetSector(uint32_t address)
{
    if (address < 0x08004000) return FLASH_SECTOR_0;
    if (address < 0x08008000) return FLASH_SECTOR_1;
    if (address < 0x0800C000) return FLASH_SECTOR_2;
    if (address < 0x08010000) return FLASH_SECTOR_3;
    if (address < 0x08020000) return FLASH_SECTOR_4;
    if (address < 0x08040000) return FLASH_SECTOR_5;
    if (address < 0x08060000) return FLASH_SECTOR_6;
    return FLASH_SECTOR_7;
}

/**
 * @brief  Erases all flash sectors from start_address to end of flash.
 * @details Unlocks flash, performs sector erase from the sector containing
 *          start_address up to SECTOR_7 (inclusive), then re-locks flash.
 *          Typically called before writing new firmware in bootloader context.
 * @param  start_address : first address to erase (sector-aligned recommended).
 * @return FLASH_OK    : erase successful.
 *         FLASH_ERROR : HAL erase failed (sector_error indicates failing sector).
 */
FLASH_Status_t SIGMA_Flash_Erase(uint32_t start_address)
{
    FLASH_EraseInitTypeDef erase_init;
    uint32_t first_sector = SIGMA_Flash_GetSector(start_address);
    uint32_t last_sector  = FLASH_SECTOR_7;
    uint32_t nb_sectors   = last_sector - first_sector + 1; // +1 : inclusive count
    uint32_t sector_error = 0;

    HAL_FLASH_Unlock();

    erase_init.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase_init.Sector       = first_sector;
    erase_init.NbSectors    = nb_sectors;

    if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return FLASH_ERROR;
    }

    HAL_FLASH_Lock();
    return FLASH_OK;
}

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
FLASH_Status_t SIGMA_Flash_Write(uint32_t address, uint8_t *data, uint32_t length)
{
    if (!data) return FLASH_ERROR;

    HAL_FLASH_Unlock();

    uint32_t i = 0;
    while (i < length)
    {
        uint32_t word = 0xFFFFFFFF;

        /* Pack up to 4 bytes into one 32-bit word (little-endian) */
        for (uint8_t b = 0; b < 4 && i < length; b++, i++)
        {
            word &= ~(0xFF << (b * 8));
            word |=  (data[i] << (b * 8));
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, word) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return FLASH_ERROR;
        }
        address += 4;
    }

    HAL_FLASH_Lock();
    return FLASH_OK;
}

/**
 * @brief  Reads bytes from flash memory using direct memory-mapped access.
 * @details Flash is memory-mapped on STM32F4 — no HAL call needed, direct
 *          pointer dereference is sufficient and faster than HAL read APIs.
 * @param  address : source flash address to read from.
 * @param  data    : pointer to destination buffer.
 * @param  length  : number of bytes to read.
 * @return FLASH_OK always (read cannot fail on valid addresses).
 */
FLASH_Status_t SIGMA_Flash_Read(uint32_t address, uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++)
    {
        data[i] = *((uint8_t *)address++);
    }
    return FLASH_OK;
}
