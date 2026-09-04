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

#endif /* INC_VEGACANMANAGER_H_ */
