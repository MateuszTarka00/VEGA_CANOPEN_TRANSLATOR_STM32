/*
 * NMT_functions.c
 *
 *  Created on: Oct 11, 2025
 *      Author: mateo
 */


#include "NMT_functions.h"
#include "OD.h"
#include "flash.h"

#define FLASH_PAGE 127

CANopenNodeSTM32 canOpenNodeSTM32;

void nmtStateChangedCallback(const CO_NMT_internalState_t state)
{
	CO_LOCK_OD(canOpenNodeSTM32.canOpenStack->CANmodule);

	OD_entry_t *entry;

	if(state != CO_NMT_OPERATIONAL)
	{

	}

	if(state == CO_NMT_OPERATIONAL)
	{

	}

	CO_UNLOCK_OD(canOpenNodeSTM32.canOpenStack->CANmodule);
}
