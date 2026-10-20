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
#include "task.h"
#include <stdio.h>

#define GENEMEK_MAX_FLOORS       24
#define GENEMEK_FLOOR_TEXT_LEN    8

typedef struct
{
    char floorTexts[GENEMEK_MAX_FLOORS][GENEMEK_FLOOR_TEXT_LEN];
    uint8_t floorCount;

} GenemekFloorConfig_t;

static GenemekFloorConfig_t genemekFloorConfig = {0};

#define GENEMEK_NODE_ID 21U
#define GENEMEK_FLOOR_TEXT_OD 0x62B0U
#define FLOOR_SDO_TIMEOUT_MS 500U

typedef enum {
    FLOOR_SDO_IDLE,
    FLOOR_SDO_UPLOAD_INIT,
    FLOOR_SDO_UPLOAD_SEGMENT,
    FLOOR_SDO_DOWNLOAD_INIT,
    FLOOR_SDO_DOWNLOAD_SEGMENT
} FloorSdoState;

static struct {
    FloorSdoState state;
    uint8_t nodeId, subIndex, toggle, offset, length, segmentLength;
    uint8_t buffer[GENEMEK_FLOOR_TEXT_LEN];
    bool sizeKnown;
    TickType_t lastActivity;
} floorSdo;
static uint8_t nextFloorText = 1U;
static uint8_t floorTextAttempts;
static bool floorTextsStarted;
static uint32_t floorTextsValid;

static bool sendFloorSdo(uint8_t command, uint16_t index, uint8_t subIndex,
                         const uint8_t *payload)
{
    uint8_t data[8] = {command, (uint8_t)index, (uint8_t)(index >> 8), subIndex};
    if (payload != NULL) {
        memcpy(&data[4], payload, 4U);
    }
    if (!protocolSend(MASTER_LOP_TX_SDO + floorSdo.nodeId, data, 8U, PROTOCOL_CANOPEN)) {
        return false;
    }
    floorSdo.lastActivity = xTaskGetTickCount();
    return true;
}

static void finishFloorSdo(bool success)
{
    if (floorSdo.state == FLOOR_SDO_UPLOAD_INIT || floorSdo.state == FLOOR_SDO_UPLOAD_SEGMENT) {
        if (success) {
            uint8_t slot = floorSdo.subIndex - 1U;
            memcpy(genemekFloorConfig.floorTexts[slot], floorSdo.buffer, GENEMEK_FLOOR_TEXT_LEN);
            floorTextsValid |= 1UL << slot;
            genemekFloorConfig.floorCount++;
            CanOpenNodeObject *current = getCanOpenObjectsList();
            while (current != NULL) {
                if (current->canOpenNodeHandler.displayedFloor == floorSdo.subIndex) {
                    current->canOpenNodeHandler.floorTextPending = true;
                }
                current = current->nextObject;
            }
        }
        if (success || ++floorTextAttempts >= 3U) {
            nextFloorText++;
            floorTextAttempts = 0U;
        }
    }
    floorSdo.state = FLOOR_SDO_IDLE;
}

static void abortFloorSdo(uint32_t code)
{
    uint8_t payload[4] = {(uint8_t)code, (uint8_t)(code >> 8),
                          (uint8_t)(code >> 16), (uint8_t)(code >> 24)};
    bool upload = floorSdo.state == FLOOR_SDO_UPLOAD_INIT || floorSdo.state == FLOOR_SDO_UPLOAD_SEGMENT;
    sendFloorSdo(0x80U, upload ? GENEMEK_FLOOR_TEXT_OD : CAR_FLOOR_TEXT_OD,
                 upload ? floorSdo.subIndex : CAR_FLOOR_TEXT_SUB, payload);
    finishFloorSdo(false);
}

static void sendFloorDownloadSegment(void)
{
    uint8_t remaining = floorSdo.length - floorSdo.offset;
    uint8_t count = remaining > 7U ? 7U : remaining;
    uint8_t data[8] = {0};
    data[0] = (uint8_t)((floorSdo.toggle << 4) | ((7U - count) << 1) | (remaining <= 7U));
    memcpy(&data[1], &floorSdo.buffer[floorSdo.offset], count);
    floorSdo.segmentLength = count;
    if (!protocolSend(MASTER_LOP_TX_SDO + floorSdo.nodeId, data, 8U, PROTOCOL_CANOPEN)) {
        finishFloorSdo(false);
        return;
    }
    floorSdo.lastActivity = xTaskGetTickCount();
}

void LOP_ResetFloorTexts(uint32_t nodeId)
{
    if (nodeId != GENEMEK_NODE_ID) { return; }
    if (floorSdo.state == FLOOR_SDO_UPLOAD_INIT || floorSdo.state == FLOOR_SDO_UPLOAD_SEGMENT) {
        floorSdo.state = FLOOR_SDO_IDLE;
    }
    memset(&genemekFloorConfig, 0, sizeof(genemekFloorConfig));
    floorTextsValid = 0U;
    nextFloorText = 1U;
    floorTextAttempts = 0U;
    floorTextsStarted = false;
}

static bool receiveFloorSdo(CanOpenNodeHandler *node, CAN_Message_t *msg)
{
    if (floorSdo.state == FLOOR_SDO_IDLE || node->canOpenID != floorSdo.nodeId || msg->len != 8U) {
        return false;
    }
    uint8_t command = msg->data[0];
    uint16_t index = (uint16_t)msg->data[1] | ((uint16_t)msg->data[2] << 8);
    bool upload = floorSdo.state == FLOOR_SDO_UPLOAD_INIT || floorSdo.state == FLOOR_SDO_UPLOAD_SEGMENT;
    bool matches = index == (upload ? GENEMEK_FLOOR_TEXT_OD : CAR_FLOOR_TEXT_OD) &&
        msg->data[3] == (upload ? floorSdo.subIndex : CAR_FLOOR_TEXT_SUB);
    if (command == 0x80U) {
        if (!matches) { return false; }
        finishFloorSdo(false);
        return true;
    }
    switch (floorSdo.state) {
    case FLOOR_SDO_UPLOAD_INIT:
        if (!matches || (command & 0xE0U) != 0x40U) { return false; }
        if (command & 0x02U) {
            floorSdo.length = (command & 1U) ? (uint8_t)(4U - ((command >> 2) & 3U)) : 4U;
            memcpy(floorSdo.buffer, &msg->data[4], floorSdo.length);
            finishFloorSdo(true);
        } else {
            uint32_t size = (uint32_t)msg->data[4] | ((uint32_t)msg->data[5] << 8) |
                ((uint32_t)msg->data[6] << 16) | ((uint32_t)msg->data[7] << 24);
            floorSdo.sizeKnown = (command & 1U) != 0U;
            if (floorSdo.sizeKnown && size > GENEMEK_FLOOR_TEXT_LEN) {
                abortFloorSdo(0x06070010UL);
                break;
            }
            floorSdo.length = (uint8_t)size;
            floorSdo.state = FLOOR_SDO_UPLOAD_SEGMENT;
            if (!sendFloorSdo(0x60U, 0U, 0U, NULL)) { finishFloorSdo(false); }
        }
        break;
    case FLOOR_SDO_UPLOAD_SEGMENT: {
        if ((command & 0xE0U) != 0U) { return false; }
        uint8_t count = 7U - ((command >> 1) & 7U);
        if (((command >> 4) & 1U) != floorSdo.toggle) {
            abortFloorSdo(0x05030000UL);
        } else if (floorSdo.offset + count > GENEMEK_FLOOR_TEXT_LEN ||
                   (!(command & 1U) && count != 7U)) {
            abortFloorSdo(0x06070010UL);
        } else {
            memcpy(&floorSdo.buffer[floorSdo.offset], &msg->data[1], count);
            floorSdo.offset += count;
            if (command & 1U) {
                if (floorSdo.sizeKnown && floorSdo.offset != floorSdo.length) {
                    abortFloorSdo(0x06070010UL);
                } else {
                    finishFloorSdo(true);
                }
            } else {
                floorSdo.toggle ^= 1U;
                if (!sendFloorSdo((uint8_t)(0x60U | (floorSdo.toggle << 4)), 0U, 0U, NULL)) {
                    finishFloorSdo(false);
                }
            }
        }
        break;
    }
    case FLOOR_SDO_DOWNLOAD_INIT:
        if (!matches || command != 0x60U) { return false; }
        if (floorSdo.length <= 4U) {
            finishFloorSdo(true);
        } else {
            floorSdo.state = FLOOR_SDO_DOWNLOAD_SEGMENT;
            sendFloorDownloadSegment();
        }
        break;
    case FLOOR_SDO_DOWNLOAD_SEGMENT:
        if ((command & 0xEFU) != 0x20U) { return false; }
        if (((command >> 4) & 1U) != floorSdo.toggle) {
            abortFloorSdo(0x05030000UL);
        } else {
            floorSdo.offset += floorSdo.segmentLength;
            if (floorSdo.offset == floorSdo.length) {
                finishFloorSdo(true);
            } else {
                floorSdo.toggle ^= 1U;
                sendFloorDownloadSegment();
            }
        }
        break;
    default:
        return false;
    }
    return true;
}

void LOP_PollFloorTexts(void)
{
    if (floorSdo.state != FLOOR_SDO_IDLE) {
        if ((xTaskGetTickCount() - floorSdo.lastActivity) >= pdMS_TO_TICKS(FLOOR_SDO_TIMEOUT_MS)) {
            abortFloorSdo(0x05040000UL);
        }
        return;
    }
    CanOpenNodeObject *current = getCanOpenObjectsList();
    while (current != NULL) {
        CanOpenNodeHandler *node = &current->canOpenNodeHandler;
        if (node->canOpenID == GENEMEK_NODE_ID && !floorTextsStarted && node->heartbeatReceived) {
            floorTextsStarted = true;
        }
        if (node->canOpenID == GENEMEK_NODE_ID && floorTextsStarted &&
            nextFloorText <= GENEMEK_MAX_FLOORS && !node->lopRequestNeeded && !node->heartbeatTimeoutError) {
            memset(&floorSdo, 0, sizeof(floorSdo));
            floorSdo.state = FLOOR_SDO_UPLOAD_INIT;
            floorSdo.nodeId = GENEMEK_NODE_ID;
            floorSdo.subIndex = nextFloorText;
            if (!sendFloorSdo(0x40U, GENEMEK_FLOOR_TEXT_OD, nextFloorText, NULL)) {
                finishFloorSdo(false);
            }
            return;
        }
        current = current->nextObject;
    }
    current = getCanOpenObjectsList();
    while (current != NULL) {
        CanOpenNodeHandler *node = &current->canOpenNodeHandler;
        if (node->floorTextPending && !node->lopRequestNeeded && !node->heartbeatTimeoutError) {
            uint8_t floor;
            taskENTER_CRITICAL();
            floor = node->displayedFloor;
            node->floorTextPending = false;
            taskEXIT_CRITICAL();
            memset(&floorSdo, 0, sizeof(floorSdo));
            floorSdo.nodeId = (uint8_t)node->canOpenID;
            if (floor >= 1U && floor <= GENEMEK_MAX_FLOORS && (floorTextsValid & (1UL << (floor - 1U)))) {
                memcpy(floorSdo.buffer, genemekFloorConfig.floorTexts[floor - 1U], GENEMEK_FLOOR_TEXT_LEN);
                while (floorSdo.length < GENEMEK_FLOOR_TEXT_LEN && floorSdo.buffer[floorSdo.length] != 0U) {
                    floorSdo.length++;
                }
            } else {
                floorSdo.length = (uint8_t)snprintf((char *)floorSdo.buffer, sizeof(floorSdo.buffer), "%u", (unsigned int)floor);
            }
            if (floorSdo.length == 0U) { floorSdo.length = 1U; }
            floorSdo.state = FLOOR_SDO_DOWNLOAD_INIT;
            uint8_t size[4] = {floorSdo.length, 0U, 0U, 0U};
            uint8_t command = floorSdo.length <= 4U ? (uint8_t)(0x23U | ((4U - floorSdo.length) << 2)) : 0x21U;
            if (!sendFloorSdo(command, CAR_FLOOR_TEXT_OD, CAR_FLOOR_TEXT_SUB,
                              floorSdo.length <= 4U ? floorSdo.buffer : size)) {
                node->floorTextPending = true;
                floorSdo.state = FLOOR_SDO_IDLE;
            }
            return;
        }
        current = current->nextObject;
    }
}

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
        if (receiveFloorSdo(node, msg)) {
            break;
        }
		/* Process SDO response message (device configuration, 8 bytes) */
		if (msg->len >= CAN_OPEN_MSG_SDO_LENGTH) {
            if ((msg->data[0] & 0xF3U) != 0x43U) {
                break;
            }
			sdoRxTx decomposedMessage;
			memcpy(decomposedMessage.data, msg->data, CAN_OPEN_MSG_SDO_LENGTH);

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
        msg.onOff = node->downLedState;

        /* Transmit message */
        protocolSend(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH, PROTOCOL_CANOPEN);

        /* Clear change flag after transmission */
        node->changeFlags &= (~DOWN_LED_STATE);
    }

    /* Handle DOWN arrival sound state change */
    if (node->changeFlags & ARRIVING_DOWN) {
        LedIndicatorMessageTx msg;

        /* Compose arrival sound message */
        msg.function = LOP_LIFT_ARRIVING_ID;
        msg.doorMap = node->doorMap;
        msg.floorNumber = node->floorNumber;
        msg.liftMap = node->liftMap;
        msg.ledIndicator = DOWN_BUTTON;
        msg.onOff = node->downArrivingSound;

        /* Transmit message */
        protocolSend(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH, PROTOCOL_CANOPEN);

        /* Clear change flag after transmission */
        node->changeFlags &= (~ARRIVING_DOWN);
    }

    /* Handle UP arrival sound state change */
    if (node->changeFlags & ARRIVING_UP) {
        LedIndicatorMessageTx msg;

        /* Compose arrival sound message */
        msg.function = LOP_LIFT_ARRIVING_ID;
        msg.doorMap = node->doorMap;
        msg.floorNumber = node->floorNumber;
        msg.liftMap = node->liftMap;
        msg.ledIndicator = UP_BUTTON;
        msg.onOff = node->upArrivingSound;

        /* Transmit message */
        protocolSend(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH, PROTOCOL_CANOPEN);

        /* Clear change flag after transmission */
        node->changeFlags &= (~ARRIVING_UP);
    }

    /* Handle special information change */
    if (node->changeFlags & SPECIAL_INFORMATION) {
        SpecialIndicationMessageTx msg;

        /* Compose special indication message */
        msg.function = LOP_SPECIAL_INDICATION_ID;
        msg.doorMap = node->doorMap;
        msg.floorNumber = node->floorNumber;
        msg.liftMap = node->liftMap;
        msg.specialIndicator = node->specialInformation;
        msg.onOff = 0x81;

        /* Transmit message */
        protocolSend(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH, PROTOCOL_CANOPEN);

        /* Clear change flag after transmission */
        node->changeFlags &= (~SPECIAL_INFORMATION);
    }

    /* Handle displayed floor indicator change */
    if (node->changeFlags & DISPLAYED_FLOOR) {
        FloorDisplayMessageTx msg = {0};
        node->floorTextPending = true;

        /* Compose floor display message */
        msg.function = LOP_LIFT_DISPLAY_FLOOR_ID;
        msg.doorMap = node->doorMap;
        msg.floorNumber = node->floorNumber;
        msg.liftMap = node->liftMap;
        msg.floorIndicator = node->displayedFloor;
        msg.onOff = TRUE;

        /* Transmit floor display message */
        protocolSend(
            MASTER_LOP_TX_ID,
            msg.data,
            CAN_OPEN_MSG_PDO_LENGTH,
            PROTOCOL_CANOPEN
        );

        /* Clear change flag */
        node->changeFlags &= (~DISPLAYED_FLOOR);
    }

    /* Handle displayed arrow indicator change */
    if (node->changeFlags & DISPLAYED_ARROW) {
        ArrowDisplayMessageTx msg;

        /* First, turn off all arrow indicators */
        msg.function = LOP_LIFT_DISPLAY_ARROW_ID;
        msg.doorMap = node->doorMap;
        msg.floorNumber = node->floorNumber;
        msg.liftMap = node->liftMap;
        msg.arrowIndicator = BOTH_ARROWS;
        msg.onOff = FALSE;

        protocolSend(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH, PROTOCOL_CANOPEN);

        /* Then, enable only the desired arrow indicator */
        msg.arrowIndicator = node->displayedArrow;
        msg.onOff = TRUE;

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
    if (floorSdo.state != FLOOR_SDO_IDLE && floorSdo.nodeId == node->canOpenID) {
        return;
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
