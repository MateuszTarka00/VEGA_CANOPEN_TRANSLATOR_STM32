/*
 * vegaCanManager.c
 *
 * VEGA Protocol Manager for STM32 Elevator Control System
 * 
 * This module handles VEGA protocol message processing and transmission.
 * VEGA is a proprietary elevator protocol layer that manages button presses
 * and status transmissions to/from elevator control systems.
 * 
 * Protocol Overview:
 * - RX (Receive): Messages from elevator panel (floor button states)
 * - TX (Transmit): Messages to elevator controller (acknowledgments, state updates)
 * - FIFO0/FIFO1: Dual CAN receive FIFOs for message buffering
 *
 *  Created on: 18 maj 2026
 *      Author: mateo
 * Refactored: 2026-09-03 - Added error handling and comprehensive documentation
 */

#include "vegaCanManager.h"
#include "flash.h"
#include "vegaCanDefinitions.h"
#include "protocolUtils.h"
#include "semphr.h"
#include "fdcan.h"

/* Extern declaration of the mutex from canOpenManager for thread-safe list access */
extern SemaphoreHandle_t canOpenNodesListMutex;

/* ============================================================================ */
/* VEGA PROTOCOL TIMING CONSTANTS (milliseconds)                             */
/* ============================================================================ */

/** @brief Connection timeout - consider disconnected after this period */
#define CONNECTED_TO_MASTER_TIMEOUT 2500

/** @brief TX interval when disconnected from master controller */
#define TIME_SEND_NOT_CONNECTED     1000

/** @brief TX interval when connected to master controller */
#define TIME_SEND_CONNECTED         100

/* ============================================================================ */
/* VEGA MESSAGE FORMAT                                                        */
/* ============================================================================ */

/** @brief Standard VEGA message payload size in bytes */
#define CAN_MESSAGE_SIZE            4

/* ============================================================================ */
/* GLOBAL VARIABLES                                                           */
/* ============================================================================ */

/** @brief Queue handle for receiving VEGA messages from CAN ISR */
QueueHandle_t vegaRxQueue;

/* ============================================================================ */
/* FORWARD DECLARATIONS - Callback functions defined later in this file       */
/* ============================================================================ */

/**
 * @brief Initialize FreeRTOS queue for VEGA message reception
 * 
 * Creates a message queue to safely pass CAN messages from ISR context
 * to the task context for VEGA protocol message processing.
 * 
 * @return None
 * @note Must be called during system initialization before CAN reception
 */
void VEGA_InitRTOS(void)
{
	vegaRxQueue = xQueueCreate(16, sizeof(CAN_Message_t));
	if (vegaRxQueue == NULL) {
		/* Queue creation failed - system is out of heap memory */
		while (1) {
			/* Halt system - unable to continue */
		}
	}

	/* NOTE: Callbacks are handled by HAL_FDCAN_RxFifo0Callback and HAL_FDCAN_RxFifo1Callback
	 * implemented in protocolUtils.c. These are strong implementations that override the weak
	 * HAL driver stubs and route messages to the appropriate queue based on FDCAN handle. */
}

/**
 * @brief Process received VEGA message and update LED states
 * 
 * Parses VEGA protocol messages to update the corresponding CANopen node's
 * LED indicator states. Button state handling is done through CANopen protocol.
 * This function only manages the LED feedback states based on VEGA status.
 * 
 * VEGA message ID to CANopen node mapping:
 * - VEGA RX message ID: 0x80 + floor number (0-19)
 * - CANopen node ID: 20 + floor number (20-39)
 * 
 * LED State Updates:
 * - Constant press (0x01/0x02): LED stays lit (indicates button pressed)
 * - Blinking (0x41/0x82): LED blinks (indicates waiting for acknowledgment)
 * 
 * @param msg Pointer to received CAN message
 * @return None
 * @warning Message parameter must be a valid non-NULL pointer
 * @note Button state updates are handled by CANopen protocol layer
 */
void processVegaMessage(CAN_Message_t *msg)
{
	/* Validate message using shared utility (checks null, length, ID range) */
	if (!validateMessage(msg, 3, 4, FIRST_RECEIVE_ID, FIRST_RECEIVE_ID + 20)) {
		return; /* Message failed validation */
	}

	/* Extract floor number from VEGA RX message ID using shared utility */
	uint8_t floorNumber = extractFloorFromVegaId(msg->id);
	if (floorNumber == 0xFF) {
		return; /* Invalid floor number (already validated by validateMessage, but defensive) */
	}

	/* Convert floor to CANopen node ID (20 + floor number) */
	uint32_t canOpenNodeId = floorToCanOpenId(floorNumber);

	/* Find corresponding CANopen node in linked list using shared utility */
	CanOpenNodeObject* nodePtr = findNodeById(getCanOpenObjectsList(), canOpenNodeId);
	if (nodePtr == NULL) {
		return; /* Cannot update non-existent node */
	}

	/* Parse LED state from third byte (byte index 2) and update using shared utility */
	switch (msg->data[2])
	{
	case DOWN_BUTTON_THIRD_BYTE_CONST_RX:
	case DOWN_BUTTON_THIRD_BYTE_BLINK_RX:
		/* DOWN button pressed or blinking - light DOWN LED */
		setLedState(&nodePtr->canOpenNodeHandler, DOWN_LED_STATE, TRUE);
		break;

	case UP_BUTTON_THIRD_BYTE_CONST_RX:
	case UP_BUTTON_THIRD_BYTE_BLINK_RX:
		/* UP button pressed or blinking - light UP LED */
		setLedState(&nodePtr->canOpenNodeHandler, UP_LED_STATE, TRUE);
		break;

	default:
		/* Unknown button state - ignore */
		break;
	}
}

/**
 * @brief Transmit VEGA protocol messages based on CANopen node states
 * 
 * Iterates through all CANopen nodes and transmits VEGA protocol messages
 * based on button states and transmission timing requirements. Handles:
 * - Single button presses (UP or DOWN)
 * - Multiple button presses (BOTH simultaneously)
 * - No button presses (idle/rest state)
 * - Adaptive TX timing based on connection status
 * 
 * Transmission timing:
 * - Connected: 100ms intervals (fast updates)
 * - Disconnected: 1000ms intervals (slow keepalive)
 * 
 * @return None
 * @note This should be called periodically from a FreeRTOS task
 */
void vegaTransmitSubTask(void)
{
	CanOpenNodeObject* canOpenObjects = getCanOpenObjectsList();
	TickType_t ticksNow = HAL_GetTick();
	uint8_t sendID = 0;
	uint8_t checksum_idx = 0;

	/* Validate node list exists */
	if (canOpenObjects == NULL) {
		return; /* No nodes to transmit for */
	}

	/* Acquire mutex to safely traverse the node list */
	if (xSemaphoreTake(canOpenNodesListMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
		return; /* Timeout acquiring mutex - skip this transmit cycle */
	}

	/* Iterate through all CANopen nodes (protected by mutex) */
	while (canOpenObjects != NULL) {
		/* Calculate transmission interval based on connection status */
		uint32_t txInterval = canOpenObjects->canOpenNodeHandler.vegaConnected ?
		                      TIME_SEND_CONNECTED : TIME_SEND_NOT_CONNECTED;

		/* Check if it's time to send next message */
		if ((ticksNow - canOpenObjects->canOpenNodeHandler.vegaTicks) < txInterval) {
			canOpenObjects = canOpenObjects->nextObject;
			continue; /* Not yet time - skip this node */
		}

		/* Calculate CAN message ID for this floor/node */
		sendID = FIRST_SEND_ID + canOpenObjects->canOpenNodeHandler.floorNumber - 1; //TODO to be checked for correctness

		/* Validate sendID doesn't exceed array bounds */
		if (sendID - FIRST_SEND_ID >= 20) {
			canOpenObjects = canOpenObjects->nextObject;
			continue; /* Floor number out of range - skip */
		}

		checksum_idx = sendID - FIRST_SEND_ID;

		/* Construct base VEGA message */
		uint8_t message[CAN_MESSAGE_SIZE] = {
		    canOpenObjects->canOpenNodeHandler.floorNumber,
		    SECOND_BYTE_VALUE, /* Always 0x0F for VEGA protocol */
		    0x00,              /* Button state byte - filled below */
		    0x00               /* Checksum byte - filled below */
		};

		/* Determine message content based on button states */
		if (canOpenObjects->canOpenNodeHandler.downButtonState &&
		    canOpenObjects->canOpenNodeHandler.upButtonState) {
			/* Both buttons pressed - transmit combined state */
			message[2] = TWO_BUTTON_THIRD_BYTE_TX;
			message[3] = inputCanLastByte[checksum_idx][3]; /* Both buttons checksum */
		}
		else if (canOpenObjects->canOpenNodeHandler.upButtonState) {
			/* Only UP button pressed */
			message[2] = UP_BUTTON_THIRD_BYTE_TX;
			message[3] = inputCanLastByte[checksum_idx][1]; /* UP button checksum */
		}
		else if (canOpenObjects->canOpenNodeHandler.downButtonState) {
			/* Only DOWN button pressed */
			message[2] = DOWN_BUTTON_THIRD_BYTE_TX;
			message[3] = inputCanLastByte[checksum_idx][2]; /* DOWN button checksum */
		}
		else {
			/* No button pressed - transmit idle/rest state */
			message[3] = inputCanLastByte[checksum_idx][0]; /* Rest state checksum */
		}

		/* Transmit VEGA message */
		protocolSend(sendID, message, CAN_MESSAGE_SIZE, PROTOCOL_VEGA);  /* VEGA protocol */

		/* Update last transmission timestamp */
		canOpenObjects->canOpenNodeHandler.vegaTicks = ticksNow;

		/* Move to next node in linked list */
		canOpenObjects = canOpenObjects->nextObject;
	}
	
	/* Release mutex after traversal complete */
	xSemaphoreGive(canOpenNodesListMutex);

	/* Release mutex after traversal complete */
	xSemaphoreGive(canOpenNodesListMutex);
}

/**
 * @brief FDCAN RX FIFO0 interrupt callback handler
 * 
 * Processes received CAN messages from FDCAN RX FIFO0. Called from interrupt
 * context when a message is received. Extracts message data and queues it
 * for processing in task context.
 * 
 * @param hfdcan Pointer to FDCAN handle
 * @param RxFifo0ITs Flags indicating which RX FIFO0 interrupt(s) occurred
 * @return None
 * @note Called from ISR context - must be fast and reentrant-safe
 * @note Implementation is in protocolUtils.c as HAL_FDCAN_RxFifo0Callback
 */

/**
 * @brief FDCAN RX FIFO1 interrupt callback handler
 * 
 * Processes received CAN messages from FDCAN RX FIFO1. Called from interrupt
 * context when a message is received. Extracts message data and queues it
 * for processing in task context.
 * 
 * @param hfdcan Pointer to FDCAN handle
 * @param RxFifo1ITs Flags indicating which RX FIFO1 interrupt(s) occurred
 * @return None
 * @note Called from ISR context - must be fast and reentrant-safe
 * @note Implementation is in protocolUtils.c as HAL_FDCAN_RxFifo1Callback
 */
