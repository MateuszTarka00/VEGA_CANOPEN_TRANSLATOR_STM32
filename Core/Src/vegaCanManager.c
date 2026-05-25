/*
 * vegaCanManager.c
 *
 *  Created on: 18 maj 2026
 *      Author: mateo
 */

#include "vegaCanManager.h"
#include "flash.h"
#include "vegaCanDefinitions.h"

#define CONNECTED_TO_MASTER_TIMEOUT 2500
#define TIME_SEND_NOT_CONNECTED		1000
#define TIME_SEND_CONNECTED			100

#define FIRST_SEND_ID	 0x200
#define FIRST_RECEIVE_ID 0x80

#define CAN_MESSAGE_SIZE	4

QueueHandle_t vegaRxQueue;

void VEGA_InitRTOS(void)
{
	vegaRxQueue = xQueueCreate(16, sizeof(CAN_Message_t));
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

void processVegaMessage(CAN_Message_t *msg)
{
	switch(msg->data[2])
	{
		case DOWN_BUTTON_THIRD_BYTE_CONST_RX:

			break;
		case UP_BUTTON_THIRD_BYTE_CONST_RX:

			break;
		case DOWN_BUTTON_THIRD_BYTE_BLINK_RX:

			break;
		case UP_BUTTON_THIRD_BYTE_BLINK_RX:

			break;
		default:
			break;
	}
}

void vegaTransmitSubTask(void)
{
	CanOpenNodeObject* canOpenObjects = getCanOpenObjectsList();
	TickType_t ticksNow = HAL_GetTick();
	uint8_t sendID = 0;

	while(canOpenObjects != NULL)
	{
		if(canOpenObjects->canOpenNodeHandler.vegaConnected)
		{
			if((ticksNow - canOpenObjects->canOpenNodeHandler.vegaTicks) < TIME_SEND_CONNECTED)
			{
				continue;
			}
		}
		else
		{
			if((ticksNow - canOpenObjects->canOpenNodeHandler.vegaTicks) < TIME_SEND_NOT_CONNECTED)
			{
				continue;
			}
		}

		sendID = FIRST_SEND_ID + canOpenObjects->canOpenNodeHandler.floorNumber;
		uint8_t message[4] = {canOpenObjects->canOpenNodeHandler.floorNumber, 0x0F, 00, 00};

		if(canOpenObjects->canOpenNodeHandler.downButtonState && canOpenObjects->canOpenNodeHandler.upButtonState)
		{
			message[2] = TWO_BUTTON_THIRD_BYTE_TX;
			message[3] = inputCanLastByte[sendID - FIRST_SEND_ID][3];

			FDCAN_Send(sendID, message, CAN_MESSAGE_SIZE);
		}
		else if(canOpenObjects->canOpenNodeHandler.upButtonState)
		{
			message[2] = UP_BUTTON_THIRD_BYTE_TX;
			message[3] = inputCanLastByte[sendID - FIRST_SEND_ID][1];

			FDCAN_Send(sendID, message, CAN_MESSAGE_SIZE);
		}
		else if(canOpenObjects->canOpenNodeHandler.downButtonState)
		{
			message[2] = DOWN_BUTTON_THIRD_BYTE_TX;
			message[3] = inputCanLastByte[sendID - FIRST_SEND_ID][2];

			FDCAN_Send(sendID, message, CAN_MESSAGE_SIZE);
		}
		else
		{
			message[3] = inputCanLastByte[sendID - FIRST_SEND_ID][0];

			FDCAN_Send(sendID, message, CAN_MESSAGE_SIZE);
		}

		canOpenObjects->canOpenNodeHandler.vegaTicks = ticksNow;

		canOpenObjects = canOpenObjects->nextObject;
	}
}

void vegaRxFifo0Callback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo0ITs)
{
    FDCAN_RxHeaderTypeDef rxHeader;
    CAN_Message_t msg;
    BaseType_t hpw = pdFALSE;


    HAL_FDCAN_GetRxMessage(
        hfdcan,
        FDCAN_RX_FIFO0,
        &rxHeader,
		msg.data
    );

	xQueueSendFromISR(
		vegaRxQueue,
		&msg,
		hpw
	);

	portYIELD_FROM_ISR(hpw);

}

void vegaRxFifo1allback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo1ITs)
{
    FDCAN_RxHeaderTypeDef rxHeader;
    CAN_Message_t msg;
    BaseType_t hpw = pdFALSE;


    HAL_FDCAN_GetRxMessage(
        hfdcan,
        FDCAN_RX_FIFO1,
        &rxHeader,
		msg.data
    );

	xQueueSendFromISR(
		vegaRxQueue,
		&msg,
		hpw
	);

	portYIELD_FROM_ISR(hpw);
}
