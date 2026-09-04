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
#include "protocolUtils.h"
#include "semphr.h"

/* Global linked list of CANopen node objects */
CanOpenNodeObject *canOpenNodesList = NULL;

/* Queue handle for receiving CANopen CAN messages - populated by ISR */
QueueHandle_t canOpenRxQueue;

/* Mutex for protecting canOpenNodesList from race conditions */
SemaphoreHandle_t canOpenNodesListMutex = NULL;

/* ============================================================================ */
/* FORWARD DECLARATIONS - Callback functions defined later in this file       */
/* ============================================================================ */

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

	/* Create mutex for protecting linked list access from race conditions */
	canOpenNodesListMutex = xSemaphoreCreateMutex();
	if (canOpenNodesListMutex == NULL) {
		/* Mutex creation failed - system is out of heap memory */
		while (1) {
			/* Halt system - unable to continue */
		}
	}

	/* NOTE: Callbacks are handled by HAL_FDCAN_RxFifo0Callback and HAL_FDCAN_RxFifo1Callback
	 * implemented in protocolUtils.c. These are strong implementations that override the weak
	 * HAL driver stubs and route messages to the appropriate queue based on FDCAN handle. */
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
	/* LOP request tracking - will be handled by CanOpenMenagerT after NMT handshake */
	node->canOpenNodeHandler.lopRequestNeeded = FALSE;
	node->canOpenNodeHandler.lastLopRequestTime = 0;
	node->canOpenNodeHandler.lopRequestAttempts = 0;
	node->canOpenNodeHandler.liftMapReceived = FALSE;
	node->canOpenNodeHandler.doorMapReceived = FALSE;
	node->nextObject = NULL;

	return node;
}

/**
 * @brief Append a new CANopen node to the linked list
 * 
 * Searches for the end of the linked list and appends a newly created node.
 * If the list is empty, the new node becomes the head.
 * 
 * THREAD-SAFE: Acquires canOpenNodesListMutex for protected access
 * 
 * @param id CANopen node ID to create
 * @return None
 */
static void appendNode(uint32_t id)
{
	/* Acquire mutex to protect list modification */
	if (xSemaphoreTake(canOpenNodesListMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
		return; /* Timeout acquiring mutex - abandon operation */
	}

	/* Create new node first */
	CanOpenNodeObject *newNode = createNode(id);
	if (newNode == NULL) {
		xSemaphoreGive(canOpenNodesListMutex);
		return; /* Node creation failed - discard */
	}

	/* If list is empty, make this the head node */
	if (canOpenNodesList == NULL) {
		canOpenNodesList = newNode;
		xSemaphoreGive(canOpenNodesListMutex);
		return;
	}

	/* Find end of list and append new node */
	CanOpenNodeObject *current = canOpenNodesList;
	while (current->nextObject != NULL) {
		current = current->nextObject;
	}

	current->nextObject = newNode;
	
	/* Release mutex */
	xSemaphoreGive(canOpenNodesListMutex);
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

	/* Search linked list for matching node ID (with mutex protection) */
	if (xSemaphoreTake(canOpenNodesListMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
		return; /* Timeout acquiring mutex */
	}

	CanOpenNodeObject *current = canOpenNodesList;
	while (current != NULL) {
		if (current->canOpenNodeHandler.canOpenID == canOpenId) {
			break; /* Found matching node */
		}
		current = current->nextObject;
	}

	xSemaphoreGive(canOpenNodesListMutex);

	/* Create new node if not found in list */
	if (current == NULL) {
		appendNode(canOpenId);
		
		/* Retrieve newly created node (with mutex protection) */
		if (xSemaphoreTake(canOpenNodesListMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
			return; /* Timeout acquiring mutex */
		}
		
		current = canOpenNodesList;
		while (current != NULL) {
			if (current->canOpenNodeHandler.canOpenID == canOpenId) {
				break;
			}
			current = current->nextObject;
		}

		xSemaphoreGive(canOpenNodesListMutex);

		if (current == NULL) {
			return; /* Failed to create/find node - critical error */
		}
	}

	/* Request device configuration from the newly found/created node */
	/* NOTE: LOP request will be sent by CanOpenMenagerT after NMT handshake complete */
	current->canOpenNodeHandler.lopRequestNeeded = TRUE;

	/* Process heartbeat and NMT state messages (COB-ID 0x700-0x7FF) */
	if (msg->id >= CAN_OPEN_HEARTBEAT_MSG_ID && msg->id <= 0x77F) {
		/* Extract NMT state from last byte of heartbeat message */
		current->canOpenNodeHandler.nmtState = msg->data[msg->len - 1];

		/* NOTE: NMT start command will be sent by CanOpenMenagerT when PRE_OPERATIONAL detected */
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

/**
 * @brief Send CANopen master heartbeat message
 * 
 * Transmits a heartbeat message from the master node (node ID 1) to indicate it is alive
 * and operational. The heartbeat is sent periodically regardless of network or
 * slave node states, making it suitable for continuous operation.
 * 
 * According to CANopen CiA 301 specification:
 * - Heartbeat COB-ID: 0x701 (0x700 + master node ID 1)
 * - Heartbeat payload: 1 byte containing the master NMT state
 * - Purpose: Network monitoring and error detection
 * 
 * This function should be called periodically (e.g., every 100-500ms) from
 * the main application loop or a FreeRTOS timer callback.
 * 
 * @return None
 * @note Master heartbeat is independent of slave node states and always transmits
 * @note Master node ID is fixed to 1 for this system
 * @see CiA 301 - Heartbeat Protocol Specification
 */
void CANOPEN_SendMasterHeartbeat(void)
{
	/* Master node ID is fixed to 1 for this elevator system */
	const uint8_t MASTER_NODE_ID = 1;

	/* Calculate heartbeat COB-ID (0x700 + node ID = 0x701) */
	uint16_t heartbeatCobId = CAN_OPEN_HEARTBEAT_MSG_ID + MASTER_NODE_ID;

	/* Construct heartbeat message: single byte with master NMT state */
	/* Master is always operational to coordinate the network */
	uint8_t heartbeatData[1] = {CO_NMT_OPERATIONAL};

	/* Transmit master heartbeat */
	protocolSend(heartbeatCobId, heartbeatData, 1, PROTOCOL_CANOPEN);
}
