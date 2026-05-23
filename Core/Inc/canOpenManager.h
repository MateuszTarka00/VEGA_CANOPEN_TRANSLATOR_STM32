/*
 * canOpenManager.h
 *
 *  Created on: 23 maj 2026
 *      Author: mateo
 */

#include "main.h"
#include "CO_app_STM32.h"
#include "FreeRTOS.h"
#include "queue.h"

typedef struct
{
	uint32_t canOpenID;
	CO_NMT_internalState_t nmtState;
	bool upButtonState;
	bool downButtonState;
	bool upLedState;
	bool downLedState;
}CanOpenNodeHandler;

typedef struct
{
	CanOpenNodeHandler canOpenNodeHandler;
	struct CanOpenNodeObject * nextObject;

}CanOpenNodeObject;

typedef struct
{
   uint32_t id;
   uint8_t len;
   uint8_t data[8];
} CAN_Message_t;

extern QueueHandle_t canOpenRxQueue;

void CANOPEN_InitRTOS(void);
void processCanOpenMessage(CAN_Message_t *msg);
