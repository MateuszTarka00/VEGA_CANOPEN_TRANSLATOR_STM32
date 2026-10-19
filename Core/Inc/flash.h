/*
 * flash.h
 *
 *  Created on: 23 paź 2025
 *      Author: mateo
 */

#ifndef INC_FLASH_H_
#define INC_FLASH_H_

#include "main.h"
#include <stdbool.h>
#include "factory_identity.h"

/** Read-only factory access. No application API erases or writes this page. */
FiResult Flash_FactoryRead(FiData *data);

/**
 * @brief Bounded CANopen NVM access; logical pages 0/1, 2 KiB each.
 * All callers must serialize writes.
 * Returns false on invalid range, unsupported bank configuration or HAL error.
 * A Flash operation/readback failure blocks all subsequent writes until reset.
 * Busy/error waits never perform PER/PG cleanup or LOCK writes on failure.
 * Read-only factory/NVM access remains available if the hardware permits it.
 */
bool Flash_NvmRead(uint8_t page, uint32_t offset, uint8_t *data, uint32_t size);
bool Flash_NvmErase(uint8_t page);
bool Flash_NvmWrite(uint8_t page, uint32_t offset, const uint8_t *data, uint32_t size);

#endif /* INC_FLASH_H_ */
