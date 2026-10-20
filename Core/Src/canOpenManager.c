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
#include <stddef.h>
#include "fdcan.h"
#include "canOpenLopDefinitions.h"
#include "semphr.h"
#include "canopen_heartbeat.h"

/* Global linked list of CANopen node objects */
CanOpenNodeObject *canOpenNodesList = NULL;

/* Queue handle for receiving CANopen CAN messages - populated by ISR */
QueueHandle_t canOpenRxQueue;

/* Mutex for protecting canOpenNodesList from race conditions */
SemaphoreHandle_t canOpenNodesListMutex = NULL;
volatile uint32_t canOpenRxDropped;
volatile uint32_t canOpenRxErrors;

static StaticQueue_t canOpenQueueControl;
static uint8_t canOpenQueueStorage[16U * sizeof(CAN_Message_t)];
static StaticSemaphore_t canOpenMutexControl;
static CanOpenNodeObject nodePool[127];
static uint8_t allocatedNodes;

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
	canOpenRxQueue = xQueueCreateStatic(16U, sizeof(CAN_Message_t),
		canOpenQueueStorage, &canOpenQueueControl);
	if (canOpenRxQueue == NULL) {
		/* Queue creation failed - system is out of heap memory */
		while (1) {
			/* Halt system - unable to continue */
		}
	}

	/* Create mutex for protecting linked list access from race conditions */
	canOpenNodesListMutex = xSemaphoreCreateMutexStatic(&canOpenMutexControl);
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
 * Takes a node from a fixed pool and initializes all fields to default values.
 * Each node represents a single elevator floor with associated state and control information.
 * 
 * @param id CANopen node ID (typically 0-127)
 * @return Pointer to newly created node, or NULL if memory allocation fails
 */
static CanOpenNodeObject* createNode(uint32_t id)
{
	CanOpenNodeObject *node;
	if (allocatedNodes >= 127U) { return NULL; }
	node = &nodePool[allocatedNodes++];

	/* Initialize all node fields to default/safe values */
	node->canOpenNodeHandler.vegaTicks = 0;
	node->canOpenNodeHandler.lastVegaTxTicks = 0;
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
	node->canOpenNodeHandler.displayedFloor = 0xFF;
	node->canOpenNodeHandler.floorTextPending = FALSE;
	node->canOpenNodeHandler.doorMap = 0;
	node->canOpenNodeHandler.liftMap = 0;
	/* LOP request tracking - will be handled by CanOpenMenagerT after NMT handshake */
	node->canOpenNodeHandler.lopRequestNeeded = FALSE;
	node->canOpenNodeHandler.lastLopRequestTime = 0;
	node->canOpenNodeHandler.lopRequestAttempts = 0;
	node->canOpenNodeHandler.liftMapReceived = FALSE;
	node->canOpenNodeHandler.doorMapReceived = FALSE;
	node->canOpenNodeHandler.heartbeatReceived = FALSE;
	node->canOpenNodeHandler.lastHeartbeatTime = 0;
	node->canOpenNodeHandler.heartbeatTimeoutError = FALSE;
	node->canOpenNodeHandler.nmtStartSent = FALSE;
	node->canOpenNodeHandler.lastNmtStartTime = 0U;
	node->canOpenNodeHandler.upArrivingSound = FALSE;
	node->canOpenNodeHandler.downArrivingSound = FALSE;
	node ->canOpenNodeHandler.specialInformation = FALSE;
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
	/* Local SDO, NMT, SYNC, EMCY and unknown traffic must not discover LOP nodes. */
	if ((canOpenId == 0U) || (canOpenId == 1U) ||
		!(((msg->id >= 0x701U) && (msg->id <= 0x77FU) && (msg->len == 1U)) ||
		  ((msg->id >= 0x581U) && (msg->id <= 0x5FFU) && (msg->len == 8U)) ||
		  ((msg->id >= 0x481U) && (msg->id <= 0x4FFU) && (msg->len >= 6U)))) {
		return;
	}
	if ((msg->id >= 0x701U) && (msg->id <= 0x77FU)) {
		CoFrame heartbeat = {0};
		heartbeat.id = (uint16_t)msg->id; heartbeat.length = msg->len;
		heartbeat.data[0] = msg->data[0];
		if (!CoHeartbeat_Valid(&heartbeat)) { return; }
	}

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
	bool nodeCreated = (current == NULL);
	if (nodeCreated) {
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

	/* Request device configuration only for a newly discovered node */
	/* NOTE: LOP request will be sent by CanOpenMenagerT after NMT handshake complete */
	if (nodeCreated) {
		current->canOpenNodeHandler.lopRequestNeeded = TRUE;
	}

	/* Process heartbeat and NMT state messages (COB-ID 0x700-0x7FF) */
	if (msg->id >= CAN_OPEN_HEARTBEAT_MSG_ID && msg->id <= 0x77F) {
		if (msg->data[0] == CO_NMT_INITIALIZING) {
			LOP_ResetFloorTexts(canOpenId);
		}
		/* Extract NMT state from last byte of heartbeat message */
		current->canOpenNodeHandler.nmtState = msg->data[msg->len - 1];
		if ((msg->data[0] == CO_NMT_INITIALIZING) || (msg->data[0] == CO_NMT_OPERATIONAL)) {
			current->canOpenNodeHandler.nmtStartSent = FALSE;
		}

		/* Record heartbeat reception for timeout monitoring */
		current->canOpenNodeHandler.heartbeatReceived = TRUE;
		current->canOpenNodeHandler.lastHeartbeatTime = xTaskGetTickCount();
		current->canOpenNodeHandler.heartbeatTimeoutError = FALSE;

		/* NOTE: NMT start command will be sent by CanOpenMenagerT when PRE_OPERATIONAL detected */
	} else {
		/* Route all other message types (button presses, SDO responses) to LOP layer */
		decomposeCanOpenMessage(&current->canOpenNodeHandler, msg);
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
 * @brief Check all managed nodes for heartbeat timeout and flag lost nodes
 * 
 * Iterates the node list and sets heartbeatTimeoutError when a node that has
 * previously received a heartbeat has not sent one within CAN_OPEN_HEARTBEAT_TIMEOUT_MS.
 * 
 * @return None
 */
void CANOPEN_CheckHeartbeatTimeouts(void)
{
	TickType_t currentTime = xTaskGetTickCount();

	if (xSemaphoreTake(canOpenNodesListMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
		return; /* Timeout acquiring mutex */
	}

	CanOpenNodeObject *current = canOpenNodesList;
	while (current != NULL) {
		CanOpenNodeHandler *node = &current->canOpenNodeHandler;
		if (node->heartbeatReceived &&
			(currentTime - node->lastHeartbeatTime) >= pdMS_TO_TICKS(CAN_OPEN_HEARTBEAT_TIMEOUT_MS)) {
			node->heartbeatTimeoutError = TRUE;
		}
		current = current->nextObject;
	}

	xSemaphoreGive(canOpenNodesListMutex);
}
