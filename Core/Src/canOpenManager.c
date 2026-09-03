/*
 * canOpenManager.c
 *  
 * CANopen Protocol Manager for STM32 Elevator Control System
 * 
 * This module manages CANopen node instances, processes incoming CAN messages,
 * and handles FDCAN communication. It maintains a linked list of elevator nodes
 * and their associated state information.
 *
 *  Created on: 23 maj 2026
 *      Author: mateo
 *  Refactored: 2026-09-03 - Added error handling and documentation
 */

#include "canOpenManager.h"
#include "stdlib.h"
#include "fdcan.h"
#include "canOpenLopDefinitions.h"

/* Global linked list of CANopen node objects */
CanOpenNodeObject *canOpenNodesList = NULL;

/* Queue handle for receiving CANopen CAN messages - populated by ISR */
QueueHandle_t canOpenRxQueue;

/**
 * @brief Initialize FreeRTOS queue for CANopen message reception
 * 
 * Creates a message queue to safely pass CAN messages from ISR context
 * to the task context. Should be called during system initialization.
 * 
 * @return None
 * @note Must be called before any CAN reception occurs
 */
void CANOPEN_InitRTOS(void)
{
	canOpenRxQueue = xQueueCreate(16, sizeof(CAN_Message_t));
	if (canOpenRxQueue == NULL) {
		/* Queue creation failed - system is out of heap memory */
		while (1) {
			/* Halt system - unable to continue */
		}
	}
}

/**
 * @brief Convert byte count to FDCAN Data Length Code (DLC)
 * 
 * Maps the number of data bytes to the appropriate FDCAN DLC value.
 * FDCAN supports 0-8 bytes of data in classic CAN format.
 * 
 * @param len Number of data bytes (0-8)
 * @return FDCAN DLC constant value
 * @note Values >8 default to FDCAN_DLC_BYTES_8 for safety
 */
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
        default: return FDCAN_DLC_BYTES_8; /* Safety fallback for invalid lengths */
    }
}

/**
 * @brief Send a CAN message via FDCAN1 interface
 * 
 * Queues a CAN message for transmission on the FDCAN1 bus. Waits for
 * available space in the TX FIFO with a timeout to prevent deadlock.
 * 
 * @param id CAN message identifier (11-bit standard ID)
 * @param data Pointer to message data (1-8 bytes)
 * @param len Number of data bytes to send (0-8)
 * @return None
 * @warning Function will spin-wait if TX FIFO is full. Consider timeout protection.
 */
void FDCAN_Send(uint16_t id, uint8_t *data, uint8_t len)
{
    FDCAN_TxHeaderTypeDef TxHeader;
    uint32_t timeoutTicks = 0;
    const uint32_t MAX_WAIT_TICKS = 100; /* ~100ms timeout */

    /* Validate input parameters */
    if (data == NULL || len > 8) {
        return; /* Invalid parameters - discard message */
    }

    /* Configure CAN message header */
    TxHeader.Identifier = id;
    TxHeader.IdType = FDCAN_STANDARD_ID;
    TxHeader.TxFrameType = FDCAN_DATA_FRAME;
    TxHeader.DataLength = FDCAN_BytesToDLC(len);
    TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
    TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
    TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    TxHeader.MessageMarker = 0;

    /* Wait for TX FIFO space with timeout to prevent deadlock */
    while (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(1)); /* Yield CPU */
        timeoutTicks++;
        if (timeoutTicks >= MAX_WAIT_TICKS) {
            return; /* Timeout - discard message */
        }
    }

    /* Queue message for transmission */
    HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &TxHeader, data);
}

/**
 * @brief Create and initialize a new CANopen node object
 * 
 * Allocates memory for a new CANopen node and initializes all fields to default values.
 * Each node represents a single elevator floor with associated state and control information.
 * 
 * @param id CANopen node ID (typically 0-127)
 * @return Pointer to newly created node, or NULL if memory allocation fails
 */
static CanOpenNodeObject* createNode(uint32_t id)
{
	/* Allocate memory for new node */
	CanOpenNodeObject *node = malloc(sizeof(CanOpenNodeObject));

	if (node == NULL) {
		return NULL; /* Memory allocation failed */
	}

	/* Initialize all node fields to default/safe values */
	node->canOpenNodeHandler.vegaTicks = 0;
	node->canOpenNodeHandler.floorNumber = ID_TO_FLOOR_NUMBER(id);
	node->canOpenNodeHandler.canOpenID = id;
	node->canOpenNodeHandler.vegaConnected = FALSE;
	node->canOpenNodeHandler.upButtonState = FALSE;
	node->canOpenNodeHandler.downButtonState = FALSE;
	node->canOpenNodeHandler.upLedState = FALSE;
	node->canOpenNodeHandler.downLedState = FALSE;
	node->canOpenNodeHandler.nmtState = CO_NMT_INITIALIZING;
	node->canOpenNodeHandler.changeFlags = 0;
	node->canOpenNodeHandler.displayedArrow = 0;
	node->canOpenNodeHandler.displayedFloor = 0;
	node->canOpenNodeHandler.doorMap = 0;
	node->canOpenNodeHandler.liftMap = 0;
	node->nextObject = NULL;

	return node;
}

/**
 * @brief Append a new CANopen node to the linked list
 * 
 * Searches for the end of the linked list and appends a newly created node.
 * If the list is empty, the new node becomes the head.
 * 
 * @param id CANopen node ID to create
 * @return None
 * @warning Not thread-safe - must be protected by critical section if called from multiple tasks
 */
static void appendNode(uint32_t id)
{
	/* Create new node first */
	CanOpenNodeObject *newNode = createNode(id);
	if (newNode == NULL) {
		return; /* Node creation failed - discard */
	}

	/* If list is empty, make this the head node */
	if (canOpenNodesList == NULL) {
		canOpenNodesList = newNode;
		return;
	}

	/* Find end of list and append new node */
	CanOpenNodeObject *current = canOpenNodesList;
	while (current->nextObject != NULL) {
		current = current->nextObject;
	}

	current->nextObject = newNode;
}

/**
 * @brief Process incoming CANopen message and route to appropriate handler
 * 
 * Extracts the node ID from the CAN message ID, locates or creates the
 * corresponding node object, and processes message-specific data including
 * heartbeat/NMT state updates.
 * 
 * @param msg Pointer to received CAN message
 * @return None
 * @warning Assumes msg pointer is valid and contains at least 1 byte of data
 */
void processCanOpenMessage(CAN_Message_t *msg)
{
	/* Validate input parameter */
	if (msg == NULL) {
		return; /* Invalid message pointer */
	}

	/* Validate message has at least 1 byte of data */
	if (msg->len < 1 || msg->len > 8) {
		return; /* Invalid message length */
	}

	/* Extract CANopen node ID from CAN message ID */
	/* CANopen COB-ID to node ID mapping: lower 7 bits contain node ID */
	uint32_t canOpenId = msg->id & 0x7F;

	/* Search linked list for matching node ID */
	CanOpenNodeObject *current = canOpenNodesList;
	while (current != NULL) {
		if (current->canOpenNodeHandler.canOpenID == canOpenId) {
			break; /* Found matching node */
		}
		current = current->nextObject;
	}

	/* Create new node if not found in list */
	if (current == NULL) {
		appendNode(canOpenId);
		/* Retrieve newly created node */
		current = canOpenNodesList;
		while (current != NULL) {
			if (current->canOpenNodeHandler.canOpenID == canOpenId) {
				break;
			}
			current = current->nextObject;
		}

		if (current == NULL) {
			return; /* Failed to create/find node - critical error */
		}
	}

	/* Request device configuration from the newly found/created node */
	LOP_RequestAssignment(&current->canOpenNodeHandler);

	/* Process heartbeat and NMT state messages (COB-ID 0x700-0x7FF) */
	if (msg->id >= 0x700 && msg->id <= 0x77F) {
		/* Extract NMT state from last byte of heartbeat message */
		current->canOpenNodeHandler.nmtState = msg->data[msg->len - 1];

		/* Handle pre-operational state */
		if (current->canOpenNodeHandler.nmtState == CO_NMT_PRE_OPERATIONAL) {
			/* Send NMT start command to transition to operational state */
			uint8_t nmtMessage[2] = {1, canOpenId}; /* Command: start, Node ID */
			FDCAN_Send(0x000, nmtMessage, 2); /* NMT command COB-ID is 0x000 */
		}
	}
}

/**
 * @brief Get pointer to the head of the CANopen nodes linked list
 * 
 * Returns the head pointer to allow iteration through all managed nodes.
 * Caller should traverse the list using the nextObject pointer.
 * 
 * @return Pointer to head of nodes list, or NULL if no nodes exist
 * @note Returns pointer to shared data - caller should not modify structure
 */
CanOpenNodeObject* getCanOpenObjectsList(void)
{
	return canOpenNodesList;
}
