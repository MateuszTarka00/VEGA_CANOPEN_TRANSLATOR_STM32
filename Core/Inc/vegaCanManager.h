/*
 * vegaCanManager.h
 *
 * VEGA Protocol Manager Header
 * 
 * Defines interfaces and data structures for managing VEGA protocol
 * communication on the STM32 elevator control system.
 *
 *  Created on: 18 maj 2026
 *      Author: mateo
 * Refactored: 2026-09-03 - Enhanced documentation and error handling
 */

#ifndef INC_VEGACANMANAGER_H_
#define INC_VEGACANMANAGER_H_

#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "fdcan.h"
#include "canOpenManager.h"

/* ============================================================================ */
/* EXPORTED VARIABLES                                                         */
/* ============================================================================ */

/** @brief Queue handle for receiving VEGA messages from CAN ISR */
extern QueueHandle_t vegaRxQueue;

/* ============================================================================ */
/* FUNCTION PROTOTYPES                                                        */
/* ============================================================================ */

/**
 * @brief Initialize FreeRTOS queue for VEGA message reception
 */
void VEGA_InitRTOS(void);

/**
 * @brief Process received VEGA message and extract button/state information
 * @param msg Pointer to received CAN message
 */
void processVegaMessage(CAN_Message_t *msg);

/**
 * @brief Transmit VEGA protocol messages based on CANopen node states
 * 
 * Iterates through all nodes and transmits VEGA messages based on button
 * states with adaptive timing (fast when connected, slow when disconnected).
 */
void vegaTransmitSubTask(void);

/**
 * @brief FDCAN RX FIFO0 interrupt callback handler
 * @param hfdcan Pointer to FDCAN handle
 * @param RxFifo0ITs FDCAN RX FIFO0 interrupt flags
 */
void vegaRxFifo0Callback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo0ITs);

/**
 * @brief FDCAN RX FIFO1 interrupt callback handler (note: naming typo preserved)
 * @param hfdcan Pointer to FDCAN handle
 * @param RxFifo1ITs FDCAN RX FIFO1 interrupt flags
 */
void vegaRxFifo1Callback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo1ITs);

#endif /* INC_VEGACANMANAGER_H_ */
