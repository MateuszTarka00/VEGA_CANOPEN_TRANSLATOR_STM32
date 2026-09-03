/*
 * canOpenManager.h
 *
 * CANopen Protocol Manager Header
 * 
 * Defines data structures and interfaces for managing CANopen nodes
 * in the elevator control system. Provides node state representation,
 * CAN message definitions, and manager function prototypes.
 *
 *  Created on: 23 maj 2026
 *      Author: mateo
 * Refactored: 2026-09-03 - Enhanced documentation and error handling
 */

#ifndef CAN_OPEN_MANAGER_H
#define CAN_OPEN_MANAGER_H

#include "main.h"
#include "CO_app_STM32.h"
#include "FreeRTOS.h"
#include "queue.h"

/* ============================================================================ */
/* PROTOCOL CONSTANTS                                                         */
/* ============================================================================ */

/** @brief Maximum PDO (Process Data Object) message length in bytes */
#define CAN_OPEN_MSG_PDO_LENGTH  6

/** @brief Maximum SDO (Service Data Object) message length in bytes */
#define CAN_OPEN_MSG_SDO_LENGTH  8

/* ============================================================================ */
/* CHANGE FLAGS ENUMERATION                                                   */
/* ============================================================================ */

/**
 * @brief Change notification flags for node state updates
 * 
 * Bitmask flags indicating which node properties have changed and need
 * to be transmitted to the elevator control system.
 */
typedef enum
{
    NONE = 0,                      /**< No state changes */
    UP_LED_STATE = 1 << 0,         /**< UP button LED indicator changed */
    DOWN_LED_STATE = 1 << 1,       /**< DOWN button LED indicator changed */
    DISPLAYED_FLOOR = 1 << 2,      /**< Floor display value changed */
    DISPLAYED_ARROW = 1 << 3,      /**< Arrow indicator (up/down) changed */

} ChangeFlags;

/* ============================================================================ */
/* NODE STATE STRUCTURE                                                       */
/* ============================================================================ */

/**
 * @brief State handler for a single CANopen elevator node
 * 
 * Represents all relevant state information for one elevator floor/node.
 * Maps between CANopen protocol state and elevator-specific control data.
 */
typedef struct
{
    uint32_t canOpenID;              /**< CANopen node ID (0-127) */
    CO_NMT_internalState_t nmtState; /**< Network Management state machine */

    uint8_t floorNumber;             /**< Building floor number (0-based) */
    uint8_t liftMap;                 /**< Bitmask of available lifts */
    uint8_t doorMap;                 /**< Bitmask of door configurations */

    uint32_t vegaTicks;              /**< Timestamp of last VEGA activity (ms) */
    bool vegaConnected;              /**< Connected to VEGA protocol network */

    bool upButtonState;              /**< UP button currently pressed */
    bool downButtonState;            /**< DOWN button currently pressed */

    bool upLedState;                 /**< UP button LED indicator state */
    bool downLedState;               /**< DOWN button LED indicator state */

    uint8_t displayedFloor;          /**< Current floor to display */
    uint8_t displayedArrow;          /**< Arrow indicator (0x00/0x10/0x20/0x30) */

    uint8_t changeFlags;             /**< Bitmask of changed properties (ChangeFlags) */

} CanOpenNodeHandler;

/* ============================================================================ */
/* LINKED LIST NODE STRUCTURE                                                 */
/* ============================================================================ */

/**
 * @brief Linked list node containing CANopen node handler and list pointer
 * 
 * Part of a singly-linked list that maintains all active CANopen nodes.
 * Allows dynamic addition of new nodes as they join the network.
 */
typedef struct CanOpenNodeObject
{
    CanOpenNodeHandler canOpenNodeHandler; /**< Node state data */
    struct CanOpenNodeObject *nextObject;  /**< Pointer to next node (NULL = end) */

} CanOpenNodeObject;

/* ============================================================================ */
/* CAN MESSAGE STRUCTURE                                                      */
/* ============================================================================ */

/**
 * @brief Generic CAN message container
 * 
 * Standardized format for passing CAN messages between ISR context
 * and task context via FreeRTOS queues.
 */
typedef struct
{
    uint32_t id;      /**< CAN message identifier (11-bit standard ID) */
    uint8_t len;      /**< Number of data bytes (0-8) */
    uint8_t data[8];  /**< Message payload data bytes */

} CAN_Message_t;

/* ============================================================================ */
/* EXPORTED VARIABLES                                                         */
/* ============================================================================ */

/** @brief Queue handle for CANopen message reception from ISR */
extern QueueHandle_t canOpenRxQueue;

/* ============================================================================ */
/* FUNCTION PROTOTYPES                                                        */
/* ============================================================================ */

/**
 * @brief Send a CAN message via FDCAN1 interface
 * @param id CAN message identifier
 * @param data Pointer to message data bytes
 * @param len Number of data bytes (0-8)
 */
void FDCAN_Send(uint16_t id, uint8_t *data, uint8_t len);

/**
 * @brief Initialize FreeRTOS queue for CANopen message reception
 */
void CANOPEN_InitRTOS(void);

/**
 * @brief Process incoming CANopen message and update node state
 * @param msg Pointer to received CAN message
 */
void processCanOpenMessage(CAN_Message_t *msg);

/**
 * @brief Get pointer to head of CANopen nodes linked list
 * @return Pointer to first node in list, or NULL if empty
 */
CanOpenNodeObject* getCanOpenObjectsList(void);

extern CanOpenNodeObject *getCanOpenObjectsList(void);


#endif /* CAN_OPEN_MANAGER_H */
