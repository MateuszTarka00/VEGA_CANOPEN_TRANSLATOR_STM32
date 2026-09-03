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
/* VEGA MESSAGE ID MAPPINGS                                                  */
/* ============================================================================ */

/** @brief Base CAN ID for TX messages (adds floor number offset) */
#define FIRST_SEND_ID               0x200

/** @brief Base CAN ID for RX messages (receives from panel) */
#define FIRST_RECEIVE_ID            0x80

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
}

/**
 * @brief Process received VEGA message and extract button/state information
 * 
 * Parses VEGA protocol messages to extract button press states and other
 * status information. This is called from the receive task after a message
 * has been dequeued from vegaRxQueue.
 * 
 * @param msg Pointer to received CAN message
 * @return None
 * @warning Message parameter must be a valid non-NULL pointer
 */
void processVegaMessage(CAN_Message_t *msg)
{
	/* Validate input parameter */
	if (msg == NULL) {
		return; /* Invalid message pointer */
	}

	/* Validate message has required data bytes */
	if (msg->len < 3) {
		return; /* Insufficient data for VEGA protocol parsing */
	}

	/* Parse button state from third byte (byte index 2) */
	switch (msg->data[2])
	{
	case DOWN_BUTTON_THIRD_BYTE_CONST_RX:
		/* DOWN button pressed (constant state) */
		break;
	case UP_BUTTON_THIRD_BYTE_CONST_RX:
		/* UP button pressed (constant state) */
		break;
	case DOWN_BUTTON_THIRD_BYTE_BLINK_RX:
		/* DOWN button blinking (flashing state) */
		break;
	case UP_BUTTON_THIRD_BYTE_BLINK_RX:
		/* UP button blinking (flashing state) */
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

	/* Iterate through all CANopen nodes */
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
		sendID = FIRST_SEND_ID + canOpenObjects->canOpenNodeHandler.floorNumber;

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
		FDCAN_Send(sendID, message, CAN_MESSAGE_SIZE);

		/* Update last transmission timestamp */
		canOpenObjects->canOpenNodeHandler.vegaTicks = ticksNow;

		/* Move to next node in linked list */
		canOpenObjects = canOpenObjects->nextObject;
	}
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
 */
void vegaRxFifo0Callback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo0ITs)
{
    FDCAN_RxHeaderTypeDef rxHeader;
    CAN_Message_t msg = {0};  /* Initialize to zero for safety */
    BaseType_t hpw = pdFALSE; /* Higher Priority Woken flag */

    /* Validate input parameter */
    if (hfdcan == NULL) {
        return; /* Invalid handle */
    }

    /* Retrieve message from hardware FIFO */
    HAL_StatusTypeDef status = HAL_FDCAN_GetRxMessage(
        hfdcan,
        FDCAN_RX_FIFO0,
        &rxHeader,
        msg.data
    );

    /* Validate message retrieval */
    if (status != HAL_OK) {
        return; /* Message retrieval failed */
    }

    /* Copy message metadata from hardware header */
    msg.id = rxHeader.Identifier;
    msg.len = DLC2LEN(rxHeader.DataLength); /* Convert FDCAN DLC to byte count */

    /* Queue message for task-context processing */
    xQueueSendFromISR(
        vegaRxQueue,
        &msg,
        &hpw
    );

    /* Yield to higher priority tasks if one was woken */
    portYIELD_FROM_ISR(hpw);
}

/**
 * @brief FDCAN RX FIFO1 interrupt callback handler
 * 
 * Processes received CAN messages from FDCAN RX FIFO1. Called from interrupt
 * context when a message is received. Extracts message data and queues it
 * for processing in task context.
 * 
 * Note: Naming typo preserved from original ("allback" instead of "callback")
 * to maintain backward compatibility if called from elsewhere.
 * 
 * @param hfdcan Pointer to FDCAN handle
 * @param RxFifo1ITs Flags indicating which RX FIFO1 interrupt(s) occurred
 * @return None
 * @note Called from ISR context - must be fast and reentrant-safe
 */
void vegaRxFifo1allback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo1ITs)
{
    FDCAN_RxHeaderTypeDef rxHeader;
    CAN_Message_t msg = {0};  /* Initialize to zero for safety */
    BaseType_t hpw = pdFALSE; /* Higher Priority Woken flag */

    /* Validate input parameter */
    if (hfdcan == NULL) {
        return; /* Invalid handle */
    }

    /* Retrieve message from hardware FIFO */
    HAL_StatusTypeDef status = HAL_FDCAN_GetRxMessage(
        hfdcan,
        FDCAN_RX_FIFO1,
        &rxHeader,
        msg.data
    );

    /* Validate message retrieval */
    if (status != HAL_OK) {
        return; /* Message retrieval failed */
    }

    /* Copy message metadata from hardware header */
    msg.id = rxHeader.Identifier;
    msg.len = DLC2LEN(rxHeader.DataLength); /* Convert FDCAN DLC to byte count */

    /* Queue message for task-context processing */
    xQueueSendFromISR(
        vegaRxQueue,
        &msg,
        &hpw
    );

    /* Yield to higher priority tasks if one was woken */
    portYIELD_FROM_ISR(hpw);
}
