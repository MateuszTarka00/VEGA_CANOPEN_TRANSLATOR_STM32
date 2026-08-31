/*
 * canOpenManager.c
 *
 *  Created on: 23 maj 2026
 *      Author: mateo
 */

#include "canOpenManager.h"
#include "stdlib.h"
#include "fdcan.h"

CanOpenNodeObject *canOpenNodesList = 0;
QueueHandle_t canOpenRxQueue;

void CANOPEN_InitRTOS(void)
{
	canOpenRxQueue = xQueueCreate(16, sizeof(CAN_Message_t));
}

static uint32_t FDCAN_BytesToDLC(uint8_t len)
{
    switch (len)
    {
        case 0: return FDCAN_DLC_BYTES_0;
        case 1: return FDCAN_DLC_BYTES_1;
        case 2: return FDCAN_DLC_BYTES_2;
        case 3: return FDCAN_DLC_BYTES_3;
        case 4: return FDCAN_DLC_BYTES_4;
        case 5: return FDCAN_DLC_BYTES_5;
        case 6: return FDCAN_DLC_BYTES_6;
        case 7: return FDCAN_DLC_BYTES_7;
        case 8: return FDCAN_DLC_BYTES_8;
        default: return FDCAN_DLC_BYTES_8; // safety fallback
    }
}

void FDCAN_Send(uint16_t id, uint8_t *data, uint8_t len)
{
    FDCAN_TxHeaderTypeDef TxHeader;

    TxHeader.Identifier = id;
    TxHeader.IdType = FDCAN_STANDARD_ID;
    TxHeader.TxFrameType = FDCAN_DATA_FRAME;
    TxHeader.DataLength = FDCAN_BytesToDLC(len);
    TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
    TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
    TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    TxHeader.MessageMarker = 0;

    while (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(1)); // yield CPU
    }

    HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &TxHeader, data);
}

CanOpenNodeObject* createNode(uint32_t id)
{
	CanOpenNodeObject *node =
		malloc(sizeof(CanOpenNodeObject));

	if(node == NULL)
		return NULL;

	node->canOpenNodeHandler.vegaTicks = 0;
	node->canOpenNodeHandler.floorNumber = 0;
	node->canOpenNodeHandler.canOpenID = id;
	node->canOpenNodeHandler.vegaConnected = FALSE;
	node->canOpenNodeHandler.upButtonState = FALSE;
	node->canOpenNodeHandler.downButtonState = FALSE;
	node->canOpenNodeHandler.upLedState = FALSE;
	node->canOpenNodeHandler.downLedState = FALSE;
	node->canOpenNodeHandler.changeFlags = 0;
	node->canOpenNodeHandler.displayedArrow = 0;
	node->canOpenNodeHandler.displayedFloor = 0;
	node->canOpenNodeHandler.doorMap = 0;
	node->canOpenNodeHandler.liftMap = 0;
	node->nextObject = NULL;

	return node;
}

void appendNode(uint32_t id)
{
	if(canOpenNodesList == NULL)
	{
		canOpenNodesList = createNode(id);
		return;
	}

	CanOpenNodeObject *current = canOpenNodesList;

	while(current->nextObject != NULL)
	{
		current = current->nextObject;
	}

	current->nextObject = createNode(id);
}

void processCanOpenMessage(CAN_Message_t *msg)
{
	CanOpenNodeObject *current = canOpenNodesList;

	uint32_t canOpenId = msg->id % 0x80;

    while (current != NULL)
    {
        if (current->canOpenNodeHandler.canOpenID == canOpenId)
        {
            break;
        }
        current = current->nextObject;
    }

    if(current == NULL)
    {
    	appendNode(canOpenId);
    }

    if(msg->id > 0x700 && msg->id < 0x780) //Heartbeat, NMT state
    {
    	current->canOpenNodeHandler.nmtState = msg->data[msg->len-1];

    	if(current->canOpenNodeHandler.nmtState == CO_NMT_PRE_OPERATIONAL)
    	{
    		uint8_t message[2] = {1, 27};
    		FDCAN_Send(0, message, 2);
    	}
    }
}

CanOpenNodeObject* getCanOpenObjectsList(void)
{
	return canOpenNodesList;
}
