/*
 * vegaCanManager.h
 *
 *  Created on: 18 maj 2026
 *      Author: mateo
 */

#ifndef INC_VEGACANMANAGER_H_
#define INC_VEGACANMANAGER_H_

#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "fdcan.h"
#include "canOpenManager.h"

QueueHandle_t vegaRxQueue;

void VEGA_InitRTOS(void);
static uint32_t FDCAN_BytesToDLC(uint8_t len);
void processVegaMessage(CAN_Message_t *msg);
void vegaTransmitSubTask(void);
void vegaRxFifo0Callback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo0ITs);
void vegaRxFifo1allback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo1ITs);

#endif /* INC_VEGACANMANAGER_H_ */
