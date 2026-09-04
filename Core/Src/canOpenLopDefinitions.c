/*
 * canOpenLopDefinitions.c
 *
 * CANopen Lift Operation Protocol (LOP) Implementation
 * 
 * This module implements the Lift Operation Protocol which bridges the
 * CANopen standard protocol with elevator-specific control logic. It includes:
 * - Message decomposition and parsing (RX)
 * - Message composition and transmission (TX)
 * - Device configuration via SDO (Service Data Object) protocol
 *
 * Created on: 27 sie 2026
 *      Author: mateo
 * Refactored: 2026-09-03 - Added error handling, bug fixes, and documentation
 */

#include "canOpenLopDefinitions.h"
#include "string.h" /* For memcpy, memset */
#include "protocolUtils.h"

/**
 * @brief Decompose and process received CANopen message into node state updates
 * 
 * Parses incoming CAN messages and updates the corresponding node's state based on
 * message type:
 * - BUTTON_LOP_RX_ID: Updates button states (UP/DOWN/BOTH)
 * - MASTER_LOP_RX_LIFT_DOOR: Processes SDO response for device configuration
 * 
 * @param node Pointer to CANopen node handler to update
 * @param msg Pointer to received CAN message
 * @return None
 * @warning Both parameters must be valid non-NULL pointers
 * @note Message data is assumed to be correctly formatted
 */
void decomposeCanOpenMessage(CanOpenNodeHandler *node, CAN_Message_t *msg)
{
	/* Validate input parameters using shared utility */
	if (!validatePointer(node) || !validatePointer(msg)) {
		return; /* Invalid parameters - discard message */
	}

	/* Validate message length */
	if (msg->len < 1 || msg->len > 8) {
		return; /* Invalid message length */
	}

	/* Validate function byte is within message bounds */
	if (msg->len <= FUNCTION_BYTE) {
		return; /* Function byte not present in message */
	}

	/* Calculate relative message ID (subtract node ID base offset) */
	uint16_t msgID = msg->id - node->canOpenID;

	switch (msgID)
	{
	case BUTTON_LOP_RX_ID:
		/* Process button press message (PDO format, 6 bytes) */
		if (msg->len >= CAN_OPEN_MSG_PDO_LENGTH && msg->data[FUNCTION_BYTE] == LOP_LIFT_CALL_FUNCTION_ID) {
			CarCallMessageTx decomposedMsg;
			memcpy(decomposedMsg.data, msg->data, CAN_OPEN_MSG_PDO_LENGTH);

			/* Update button states based on received value using shared utility */
			switch (decomposedMsg.button)
			{
			case DOWN_BUTTON:
				setButtonState(node, DOWN_BUTTON, decomposedMsg.onOff);
				break;
			case UP_BUTTON:
				setButtonState(node, UP_BUTTON, decomposedMsg.onOff);
				break;
			case BOTH_BUTTONS:
				/* Both buttons pressed simultaneously */
				setButtonState(node, UP_BUTTON, decomposedMsg.onOff);
				setButtonState(node, DOWN_BUTTON, decomposedMsg.onOff);
				break;
			case NO_BUTTON:
			default:
				/* No button pressed - clear all button states */
				setButtonState(node, UP_BUTTON, FALSE);
				setButtonState(node, DOWN_BUTTON, FALSE);
				break;
			}
		}
		break; /* Exit switch - DO NOT fall through to next case */

	case MASTER_LOP_RX_LIFT_DOOR:
		/* Process SDO response message (device configuration, 8 bytes) */
		if (msg->len >= CAN_OPEN_MSG_SDO_LENGTH) {
			sdoRxTx decomposedMessage;
			memcpy(decomposedMessage.data, msg->data, CAN_OPEN_MSG_SDO_LENGTH);

			/* Parse SDO command field to determine response type */
		uint8_t expedited = (decomposedMessage.command >> 1) & 0x01; /* Bit 1: expedited transfer */
		uint8_t sizeIndicated = decomposedMessage.command & 0x01;    /* Bit 0: size indicated */
		uint8_t unusedBytes = (decomposedMessage.command >> 2) & 0x03; /* Bits 3-2: unused bytes count */

		/* Calculate actual data length for expedited transfers (for future use) */
		if (expedited && sizeIndicated) {
			/* dataLength = 4 - unusedBytes; */ /* Valid range: 1-4 bytes */
		}

		/* Update node configuration based on Object Dictionary index */
		switch (decomposedMessage.index)
		{
			case LIFT_MASK:
				/* Lift availability mask - indicates which lifts are available */
				node->liftMap = decomposedMessage.returnedData[0];
				node->liftMapReceived = TRUE;
				/* Clear LOP request flag only when BOTH liftMap AND doorMap have been received */
				if (node->doorMapReceived) {
					node->lopRequestNeeded = FALSE;
					node->lopRequestAttempts = 0;
				}
				break;
			case FLOOR_NUMBER:
				/* Current floor number in the building */
				node->floorNumber = decomposedMessage.returnedData[0];
				break;
			case DOOR_MASK:
				/* Door configuration mask - indicates which doors are active */
				node->doorMap = decomposedMessage.returnedData[0];
				node->doorMapReceived = TRUE;
				/* Clear LOP request flag only when BOTH liftMap AND doorMap have been received */
				if (node->liftMapReceived) {
					node->lopRequestNeeded = FALSE;
					node->lopRequestAttempts = 0;
				}
				break;
			default:
				/* Unknown Object Dictionary entry - ignore */
				break;
			}
		}
		break;

	default:
		/* Message type not recognized - ignore */
		break;
	}
}

/**
 * @brief Compose and transmit messages based on node state changes
 * 
 * Checks the node's changeFlags to determine which messages need to be sent.
 * Composes properly formatted CANopen messages and transmits them via FDCAN.
 * Each change flag is cleared after successful transmission.
 * 
 * Message types:
 * - UP_LED_STATE: Transmit UP button LED indicator status
 * - DOWN_LED_STATE: Transmit DOWN button LED indicator status  
 * - DISPLAYED_FLOOR: Transmit current floor display value
 * - DISPLAYED_ARROW: Transmit up/down arrow display indicators
 * 
 * @param node Pointer to CANopen node handler with pending changes
 * @return None
 * @warning Node parameter must be a valid non-NULL pointer
 */
void processNodeToSendMsg(CanOpenNodeHandler *node)
{
	/* Validate input parameter */
	if (node == NULL) {
		return; /* Invalid node pointer */
	}

	/* Handle UP button LED state change */
	if (node->changeFlags & UP_LED_STATE) {
		LedIndicatorMessageTx msg;

		/* Compose LED indicator message */
		msg.function = LOP_LIFT_CALL_FUNCTION_ID;
		msg.doorMap = node->doorMap;
		msg.floorNumber = node->floorNumber;
		msg.liftMap = node->liftMap;
		msg.ledIndicator = UP_BUTTON;
		msg.onOff = node->upLedState;

		/* Transmit message */
		protocolSend(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH, PROTOCOL_CANOPEN);

		/* Clear change flag after transmission */
		node->changeFlags &= (~UP_LED_STATE);
	}

	/* Handle DOWN button LED state change */
	if (node->changeFlags & DOWN_LED_STATE) {
		LedIndicatorMessageTx msg;

		/* Compose LED indicator message */
		msg.function = LOP_LIFT_CALL_FUNCTION_ID;
		msg.doorMap = node->doorMap;
		msg.floorNumber = node->floorNumber;
		msg.liftMap = node->liftMap;
		msg.ledIndicator = DOWN_BUTTON;
		msg.onOff = node->downLedState; /* BUG FIX: Was using upLedState, should use downLedState */

		/* Transmit message */
		protocolSend(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH, PROTOCOL_CANOPEN);

		/* Clear change flag after transmission */
		node->changeFlags &= (~DOWN_LED_STATE);
	}

	/* Handle displayed floor indicator change */
	if (node->changeFlags & DISPLAYED_FLOOR) {
		FloorDisplayMessageTx msg;

		/* Compose floor display message */
		msg.function = LOP_LIFT_DISPLAY_FLOOR_ID;
		msg.doorMap = node->doorMap;
		msg.floorNumber = node->floorNumber;
		msg.liftMap = node->liftMap;
		msg.floorIndicator = node->displayedFloor;
		msg.onOff = TRUE; /* Always enable floor display */

		/* Transmit message */
		protocolSend(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH, PROTOCOL_CANOPEN);

		/* Clear change flag after transmission */
		node->changeFlags &= (~DISPLAYED_FLOOR);
	}

	/* Handle displayed arrow indicator change */
	if (node->changeFlags & DISPLAYED_ARROW) {
		ArrowDisplayMessageTx msg;

		/* First, turn off all arrow indicators (BOTH_ARROWS = 0x30) */
		msg.function = LOP_LIFT_DISPLAY_ARROW_ID;
		msg.doorMap = node->doorMap;
		msg.floorNumber = node->floorNumber;
		msg.liftMap = node->liftMap;
		msg.arrowIndicator = BOTH_ARROWS; /* Clear all arrows */
		msg.onOff = FALSE; /* Disable all arrows */

		protocolSend(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH, PROTOCOL_CANOPEN);

		/* Then, enable only the desired arrow indicator */
		msg.arrowIndicator = node->displayedArrow;
		msg.onOff = TRUE; /* Enable selected arrow */

		protocolSend(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH, PROTOCOL_CANOPEN);

		/* Clear change flag after transmission */
		node->changeFlags &= (~DISPLAYED_ARROW);
	}
}

/**
 * @brief Request device configuration via CANopen SDO (Service Data Object) protocol
 * 
 * Issues SDO read requests to obtain device configuration data (lift mask, door mask)
 * from the associated CANopen node. These requests are answered with SDO responses
 * which are processed by decomposeCanOpenMessage().
 * 
 * Requests sent:
 * - DOOR_MASK: Which doors are available
 * - LIFT_MASK: Which lifts are available
 * 
 * @param node Pointer to CANopen node handler to configure
 * @return None
 * @warning Node parameter must be a valid non-NULL pointer with valid canOpenID
 * @note This function initiates requests; responses are processed asynchronously
 */
void LOP_RequestAssignment(CanOpenNodeHandler *node)
{
	/* Validate input parameter */
	if (node == NULL) {
		return; /* Invalid node pointer */
	}

	sdoRxTx msg;
	/* Calculate message ID: base address + node ID offset */
	uint32_t msgID = MASTER_LOP_TX_LIFT_DOOR + node->canOpenID;

	/* Request 1: Door Mask configuration */
	/* Compose SDO read request command */
	msg.command = LOP_SDO_READ_TX;
	msg.index = DOOR_MASK; /* Object Dictionary address for door configuration */
	msg.subIndex = 0;      /* No sub-index needed for single-byte values */
	memset(msg.returnedData, 0, sizeof(msg.returnedData)); /* Clear response area */

	protocolSend(msgID, msg.data, CAN_OPEN_MSG_SDO_LENGTH, PROTOCOL_CANOPEN);

	/* Request 2: Lift Mask configuration */
	/* Compose SDO read request command */
	msg.command = LOP_SDO_READ_TX;
	msg.index = LIFT_MASK; /* Object Dictionary address for lift configuration */
	msg.subIndex = 0;      /* No sub-index needed for single-byte values */
	memset(msg.returnedData, 0, sizeof(msg.returnedData)); /* Clear response area */

	protocolSend(msgID, msg.data, CAN_OPEN_MSG_SDO_LENGTH, PROTOCOL_CANOPEN);
}
