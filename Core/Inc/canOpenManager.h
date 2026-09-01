/*
 * canOpenManager.h
 *
 *  Created on: 23 maj 2026
 *      Author: mateo
 */

#ifndef CAN_OPEN_MANAGER_H
#define CAN_OPEN_MANAGER_H


#include "main.h"
#include "CO_app_STM32.h"
#include "FreeRTOS.h"
#include "queue.h"

#define CAN_OPEN_MSG_PDO_LENGTH  6
#define CAN_OPEN_MSG_SDO_LENGTH  8

typedef enum
{
    NONE = 0,
    UP_LED_STATE    = 1 << 0,
    DOWN_LED_STATE  = 1 << 1,
    DISPLAYED_FLOOR = 1 << 2,
    DISPLAYED_ARROW = 1 << 3,

} ChangeFlags;

typedef struct
{
    uint32_t canOpenID;

    CO_NMT_internalState_t nmtState;

    uint8_t floorNumber;
    uint8_t liftMap;
    uint8_t doorMap;

    uint32_t vegaTicks;

    bool vegaConnected;

    bool upButtonState;
    bool downButtonState;

    bool upLedState;
    bool downLedState;

    uint8_t displayedFloor;
    uint8_t displayedArrow;

    uint8_t changeFlags;

} CanOpenNodeHandler;

typedef struct CanOpenNodeObject
{
    CanOpenNodeHandler canOpenNodeHandler;

    struct CanOpenNodeObject *nextObject;

} CanOpenNodeObject;

typedef struct
{
    uint32_t id;
    uint8_t len;
    uint8_t data[8];

} CAN_Message_t;


extern QueueHandle_t canOpenRxQueue;

void FDCAN_Send(uint16_t id, uint8_t *data, uint8_t len);

void CANOPEN_InitRTOS(void);

void processCanOpenMessage(CAN_Message_t *msg);

extern CanOpenNodeObject *getCanOpenObjectsList(void);


#endif /* CAN_OPEN_MANAGER_H */
