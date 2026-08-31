/*
 * canOpenLopDefinitions.c
 *
 *  Created on: 27 sie 2026
 *      Author: mateo
 */


#include "canOpenLopDefinitions.h"
#include "canOpenManager.h"

void decomposeCanOpenMessage(CanOpenNodeHandler *node, CAN_Message_t *msg)
{
	uint16_t msgID = msg->id - node->canOpenID;

	switch(msgID)
	{
	case BUTTON_LOP_RX_ID:
		switch(msg->data[FUNCTION_BYTE])
		{
		case LOP_LIFT_CALL_FUNCTION_ID:
			CarCallMessageTx decomposedMsg;
			memcpy(decomposedMsg.data, msg->data, CAN_OPEN_MSG_PDO_LENGTH);

			switch(decomposedMsg.button) //Check which button was pressed
			{
			case DOWN_BUTTON:
				node->downButtonState = decomposedMsg.button;
				break;
			case UP_BUTTON:
				node->upButtonState = decomposedMsg.button;
				break;
			case BOTH_BUTTONS:
				node->upButtonState = decomposedMsg.button;
				node->downButtonState = decomposedMsg.button;
				break;
			default:
				break;
			}
			break;
		}
	case MASTER_LOP_RX_LIFT_DOOR:
		sdoRxTx decomposedMessage;
		memcpy(decomposedMessage.data, msg->data, CAN_OPEN_MSG_SDO_LENGTH);

		uint8_t expedited = (decomposedMessage.command >> 1) & 0x01;
		uint8_t sizeIndicated = decomposedMessage.command & 0x01;
		uint8_t unusedBytes = (decomposedMessage.command >> 2) & 0x03;

		uint8_t dataLength = 0;

		if (expedited && sizeIndicated)
		{
			dataLength = 4 - unusedBytes;
		}

		switch(decomposedMessage.index)
		{
		case LIFT_MASK:
			node->liftMap = decomposedMessage.returnedData[0];
			break;
		case FLOOR_NUMBER:
			node->floorNumber = decomposedMessage.returnedData[0];
			break;
		case DOOR_MASK:
			node->doorMap = decomposedMessage.returnedData[0];
			break;
		default:
			break;
		}
	default:
		break;
	}
}

void processNodeToSendMsg(CanOpenNodeHandler *node)
{
	if(node->changeFlags & UP_LED_STATE)
	{
		LedIndicatorMessageTx msg;

		msg.function = LOP_LIFT_CALL_FUNCTION_ID;
		msg.doorMap = node->doorMap;
		msg.floorNumber = node->floorNumber;
		msg.liftMap = node->liftMap;
		msg.ledIndicator = UP_BUTTON;
		msg.onOff = node->upLedState;

		FDCAN_Send(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH);

		node->changeFlags &= (~UP_LED_STATE);
	}

	if(node->changeFlags & DOWN_LED_STATE)
	{
		LedIndicatorMessageTx msg;

		msg.function = LOP_LIFT_CALL_FUNCTION_ID;
		msg.doorMap = node->doorMap;
		msg.floorNumber = node->floorNumber;
		msg.liftMap = node->liftMap;
		msg.ledIndicator = DOWN_BUTTON;
		msg.onOff = node->upLedState;

		FDCAN_Send(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH);

		node->changeFlags &= (~DOWN_LED_STATE);
	}

	if(node->changeFlags & DISPLAYED_FLOOR)
	{
		FloorDisplayMessageTx msg;

		msg.function = LOP_LIFT_DISPLAY_FLOOR_ID;
		msg.doorMap = node->doorMap;
		msg.floorNumber = node->floorNumber;
		msg.liftMap = node->liftMap;
		msg.floorIndicator = node->displayedFloor;
		msg.onOff = TRUE;

		FDCAN_Send(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH);

		node->changeFlags &= (~DISPLAYED_FLOOR);
	}

	if(node->changeFlags & DISPLAYED_ARROW)
	{
		ArrowDisplayMessageTx msg;

		msg.function = LOP_LIFT_DISPLAY_ARROW_ID;
		msg.doorMap = node->doorMap;
		msg.floorNumber = node->floorNumber;
		msg.liftMap = node->liftMap;
		msg.arrowIndicator = BOTH_ARROWS;
		msg.onOff = FALSE;

		FDCAN_Send(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH); //TODO to be checked if it is working

		msg.arrowIndicator = node->displayedArrow;
		msg.onOff = TRUE;

		FDCAN_Send(MASTER_LOP_TX_ID, msg.data, CAN_OPEN_MSG_PDO_LENGTH);

		node->changeFlags &= (~DISPLAYED_ARROW);
	}
}
