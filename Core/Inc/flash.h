/*
 * flash.h
 *
 *  Created on: 23 paź 2025
 *      Author: mateo
 */

#ifndef INC_FLASH_H_
#define INC_FLASH_H_

#include "main.h"

typedef struct
{
	uint8_t virtualInputs[16][6];
	uint8_t virtualOutputs[16][6];
}Flash_virtualInputOutput;

extern Flash_virtualInputOutput flash_virtualInputOutput;

void Flash_ErasePage(uint32_t pageIndex);
void Flash_WriteStruct(uint32_t pageIndex, const Flash_virtualInputOutput *data);
void Flash_ReadStruct(uint32_t pageIndex, Flash_virtualInputOutput *data);
uint32_t Flash_GetPageAddress(uint32_t pageIndex);
uint8_t checkStructEmpty(const Flash_virtualInputOutput *data);


#endif /* INC_FLASH_H_ */
