/*
 * flash.c
 *
 *  Created on: 23 paź 2025
 *      Author: mateo
 */

#include "flash.h"
#include <string.h>

/* Reserved by both linker scripts; no collision with application sections. */
extern uint8_t __canopen_nvm_start__[];
extern uint8_t __factory_identity_start__[];

static bool geometryValid(void)
{
    return (FLASH_SIZE == DEVICE_FLASH_SIZE) &&
        (FLASH_PAGE_SIZE == DEVICE_FLASH_PAGE_SIZE) &&
        ((FLASH_BANK_NB == 1U) || (FLASH_BANK_NB == 2U)) &&
        ((FLASH_BANK_NB != 2U) || ((FLASH->OPTR & FLASH_OPTR_nSWAP_BANK) != 0U));
}

FiResult Flash_FactoryRead(FiData *data)
{
    if (data == NULL) { return FI_INVALID; }
    memset(data, 0, sizeof(*data));
    if (!geometryValid() ||
        ((uintptr_t)__factory_identity_start__ != FI_FLASH_ADDRESS)) {
        return FI_IO_ERROR;
    }
    return Fi_Decode(__factory_identity_start__, FI_RECORD_SIZE, data);
}

static bool nvmRange(uint8_t page, uint32_t offset, uint32_t size)
{
    return (page < 2U) && (offset <= FLASH_PAGE_SIZE) &&
        (size <= FLASH_PAGE_SIZE - offset) &&
        geometryValid() &&
        ((uintptr_t)__canopen_nvm_start__ == FI_NVM_ADDRESS);
}

bool Flash_NvmRead(uint8_t page, uint32_t offset, uint8_t *data, uint32_t size)
{
    if ((data == NULL) || !nvmRange(page, offset, size)) { return false; }
    memcpy(data, &__canopen_nvm_start__[(uint32_t)page * FLASH_PAGE_SIZE + offset], size);
    return true;
}

bool Flash_NvmErase(uint8_t page)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t error = 0U;
    uint32_t address;
    DeviceEraseSpan span;
    HAL_StatusTypeDef status, lockStatus;
    if (!nvmRange(page, 0U, FLASH_PAGE_SIZE)) { return false; }
    address = (uint32_t)(uintptr_t)__canopen_nvm_start__ + (uint32_t)page * FLASH_PAGE_SIZE;
    if (!DeviceMemory_EraseSpan(address - FLASH_BASE, FLASH_PAGE_SIZE,
                               FLASH_BANK_NB, &span)) { return false; }
    erase.TypeErase = FLASH_TYPEERASE_PAGES; erase.NbPages = span.pages;
    erase.Banks = (span.bank == 1U) ? FLASH_BANK_1 : FLASH_BANK_2;
    erase.Page = span.page;
    if (FLASH_GuardFailure(HAL_FLASH_Unlock()) != HAL_OK) { return false; }
    status = FLASH_GuardFailure(HAL_FLASHEx_Erase(&erase, &error));
    lockStatus = (status == HAL_OK) ? HAL_FLASH_Lock() : status;
    (void)FLASH_GuardFailure(lockStatus);
    return (status == HAL_OK) && (lockStatus == HAL_OK);
}

bool Flash_NvmWrite(uint8_t page, uint32_t offset, const uint8_t *data, uint32_t size)
{
    uint32_t i, address;
    HAL_StatusTypeDef status = HAL_OK, lockStatus;
    if ((data == NULL) || !nvmRange(page, offset, size) ||
        ((offset % 8U) != 0U) || ((size % 8U) != 0U)) { return false; }
    address = (uint32_t)(uintptr_t)__canopen_nvm_start__ + (uint32_t)page * FLASH_PAGE_SIZE + offset;
    if (FLASH_GuardFailure(HAL_FLASH_Unlock()) != HAL_OK) { return false; }
    for (i = 0U; i < size; i += 8U) {
        uint64_t word;
        memcpy(&word, &data[i], sizeof(word));
        status = FLASH_GuardFailure(
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address + i, word));
        if (status != HAL_OK) { break; }
    }
    lockStatus = (status == HAL_OK) ? HAL_FLASH_Lock() : status;
    (void)FLASH_GuardFailure(lockStatus);
    if ((status != HAL_OK) || (lockStatus != HAL_OK)) { return false; }
    if (memcmp((const void *)(uintptr_t)address, data, size) != 0) {
        (void)FLASH_GuardFailure(HAL_ERROR);
        return false;
    }
    return true;
}
