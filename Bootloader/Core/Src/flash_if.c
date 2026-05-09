#include "stm32f4xx.h"
#include <stdint.h>

#define APP_A_BASE_ADDR   0x08008000UL
#define APP_A_END_ADDR    0x080FFFFFUL

static uint32_t flash_sector_by_addr(uint32_t addr)
{
    if (addr < 0x08004000UL) return FLASH_SECTOR_0;
    if (addr < 0x08008000UL) return FLASH_SECTOR_1;
    if (addr < 0x0800C000UL) return FLASH_SECTOR_2;
    if (addr < 0x08010000UL) return FLASH_SECTOR_3;
    if (addr < 0x08020000UL) return FLASH_SECTOR_4;
    if (addr < 0x08040000UL) return FLASH_SECTOR_5;
    if (addr < 0x08060000UL) return FLASH_SECTOR_6;
    if (addr < 0x08080000UL) return FLASH_SECTOR_7;
    if (addr < 0x080A0000UL) return FLASH_SECTOR_8;
    if (addr < 0x080C0000UL) return FLASH_SECTOR_9;
    if (addr < 0x080E0000UL) return FLASH_SECTOR_10;
    return FLASH_SECTOR_11;
}

int flash_if_erase_app(uint32_t app_start_addr)
{
    if (app_start_addr != APP_A_BASE_ADDR) {
        return -1;
    }

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = flash_sector_by_addr(APP_A_BASE_ADDR);
    erase.NbSectors = flash_sector_by_addr(APP_A_END_ADDR) - erase.Sector + 1;

    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&erase, &sector_error);
    HAL_FLASH_Lock();

    return (st == HAL_OK) ? 0 : -2;
}

int flash_if_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if ((addr < APP_A_BASE_ADDR) || ((addr + len - 1U) > APP_A_END_ADDR)) {
        return -1;
    }

    HAL_FLASH_Unlock();

    for (uint32_t i = 0; i < len; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, addr + i, data[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return -2;
        }
    }

    HAL_FLASH_Lock();
    return 0;
}
