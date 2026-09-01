/*
 * canOpenLopDefinitions.h
 *
 *  Created on: 23 sie 2026
 *      Author: mateo
 */

#ifndef INC_CANOPENLOPDEFINITIONS_H_
#define INC_CANOPENLOPDEFINITIONS_H_

#include "main.h"
#include "canOpenManager.h"

#define MASTER_LOP_TX_ID 0x400
#define BUTTON_LOP_RX_ID 0x480
#define CAN_OPEN_RX_ID(NODE_ID) 0x480 + NODE_ID
#define FLOOR_TO_NODE_ID(floor) (20 + floor)
#define ID_TO_FLOOR_NUMBER(id) (id - 20)

#define MASTER_LOP_TX_LIFT_DOOR 0x600
#define MASTER_LOP_RX_LIFT_DOOR 0x580

//Functions IDs
#define LOP_LIFT_CALL_FUNCTION_ID	0x02
#define LOP_LIFT_DISPLAY_FLOOR_ID	0x40
#define LOP_LIFT_DISPLAY_ARROW_ID	0x42
#define LOP_SDO_READ_TX				0x40
#define LOP_SDO_READ_RX_MASK		0x0C //number of unsued bytes in response bits
#define LOP_SDO_READ_RX				0x4F & (~0x0C)

//Bytes purpose
#define FUNCTION_BYTE		0
#define INDICATOR_BYTE  	1
#define LIFT_NUMBER_BYTE	2
#define FLOOR_NUMBER_BYTE	3
#define DOOR_NUMBER_BYTE	4
#define ACTIVATION_BYTE		5

//OD addresses (little endian)
#define LIFT_MASK 		0x0160 //0x6001
#define FLOOR_NUMBER	0x0260 //0x6002
#define DOOR_MASK		0x0360 //0x6003

typedef enum
{
	NO_BUTTON,
	UP_BUTTON,
	DOWN_BUTTON,
	BOTH_BUTTONS
}CallButtonsEnum;

typedef enum
{
	NO_ARROW = 0x00,
	UP_ARROW= 0x10,
	DOWN_ARROW = 0x20,
	BOTH_ARROWS = 0x30
}ArrowsEnum;

typedef union
{
    struct
    {
        uint8_t function;
        uint8_t floorIndicator;
        uint8_t liftMap;
        uint8_t floorNumber;
        uint8_t doorMap;
        uint8_t onOff;
    };

    uint8_t data[6];

} FloorDisplayMessageTx;


typedef union
{
    struct
    {
        uint8_t function;
        uint8_t arrowIndicator;
        uint8_t liftMap;
        uint8_t floorNumber;
        uint8_t doorMap;
        uint8_t onOff;
    };

    uint8_t data[6];

} ArrowDisplayMessageTx;


typedef union
{
    struct
    {
        uint8_t function;
        uint8_t ledIndicator;
        uint8_t liftMap;
        uint8_t floorNumber;
        uint8_t doorMap;
        uint8_t onOff;
    };

    uint8_t data[6];

} LedIndicatorMessageTx;


typedef union
{
    struct
    {
        uint8_t function;
        uint8_t soundIndicator;
        uint8_t liftMap;
        uint8_t floorNumber;
        uint8_t doorMap;
        uint8_t onOff;
    };

    uint8_t data[6];

} ArrivingMessageTx;


typedef union
{
    struct
    {
        uint8_t function;
        uint8_t button;
        uint8_t liftMap;
        uint8_t floorNumber;
        uint8_t doorMap;
        uint8_t onOff;
    };

    uint8_t data[6];

} CarCallMessageTx;

typedef union
{
    struct __attribute__((packed))
    {
        uint8_t  command;         // byte 0
        uint16_t index;           // byte 1-2
        uint8_t  subIndex;        // byte 3
        uint8_t  returnedData[4]; // byte 4-7
    };

    uint8_t data[8];

} sdoRxTx;

void decomposeCanOpenMessage(CanOpenNodeHandler *node, CAN_Message_t *msg);
void processNodeToSendMsg(CanOpenNodeHandler *node);
void LOP_RequestAssignment(CanOpenNodeHandler *node);

#endif /* INC_CANOPENLOPDEFINITIONS_H_ */
