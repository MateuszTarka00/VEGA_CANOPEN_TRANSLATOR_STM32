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

#include <stdbool.h>
#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "fdcan.h"

/* Boolean value definitions for compatibility */
#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

/* ============================================================================ */
/* PROTOCOL CONSTANTS                                                         */
/* ============================================================================ */

/** @brief Maximum PDO (Process Data Object) message length in bytes */
#define CAN_OPEN_MSG_PDO_LENGTH  6

/** @brief Maximum SDO (Service Data Object) message length in bytes */
#define CAN_OPEN_MSG_SDO_LENGTH  8

/** @brief Heartbeat message ID*/
#define CAN_OPEN_HEARTBEAT_MSG_ID  0x700  /**< Heartbeat message ID */

/* ============================================================================ */
/* FDCAN UTILITY FUNCTION DECLARATIONS                                        */
/* ============================================================================ */

/**
 * @brief Convert FDCAN DLC to data length in bytes
 * @param dlc FDCAN data length code
 * @return Number of data bytes (0-8)
 * @note Implemented in protocolUtils.c as shared utility
 */
#define DLC2LEN(dlc) ((dlc <= 8) ? (dlc) : 8)

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
/* CANOPEN NMT STATE ENUMERATION                                              */
/* ============================================================================ */

/**
 * @brief CANopen Network Management (NMT) state machine states
 * 
 * Defines the lifecycle states for CANopen devices according to CiA 301 standard.
 * The NMT state machine controls device behavior and communication capabilities.
 * 
 * State Transitions:
 * - INITIALIZING → PRE_OPERATIONAL (after initialization complete)
 * - PRE_OPERATIONAL ↔ OPERATIONAL (via NMT start command)
 * - Any state → STOPPED (via NMT stop command)
 * 
 * @note State values must match CANopen CiA 301 specification for protocol compliance
 * @see CiA 301 - CANopen Application Layer and Communication Profile
 */
typedef enum
{
    /** @brief Initializing state (0x00)
     * 
     * Device is initializing hardware and communication layers.
     * No CANopen communication is active.
     * Transitions to PRE_OPERATIONAL when initialization completes.
     */
    CO_NMT_INITIALIZING = 0x00,

    /** @brief Pre-operational state (0x7F)
     * 
     * Device is initialized and can receive PDO/SDO communication.
     * But does NOT transmit or receive PDO data.
     * Used for device configuration and parameter setup.
     * Heartbeat messages are transmitted to signal device presence.
     * Transitions to OPERATIONAL via NMT start command.
     */
    CO_NMT_PRE_OPERATIONAL = 0x7F,

    /** @brief Operational state (0x05)
     * 
     * Device is fully operational.
     * PDO (Process Data Objects) are transmitted and received.
     * Regular data exchange occurs with all CANopen devices on network.
     * Device performs its normal application functions.
     */
    CO_NMT_OPERATIONAL = 0x05,

    /** @brief Stopped state (0x04)
     * 
     * Device is stopped.
     * No PDO communication is active.
     * Can only receive NMT commands and heartbeat monitoring is disabled.
     * Used for emergency stop or device shutdown scenarios.
     */
    CO_NMT_STOPPED = 0x04

} CanOpenNMTState;

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
    CanOpenNMTState nmtState;        /**< Network Management state machine */

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

    /* LOP Request state tracking - managed by CanOpenMenagerT only */
    bool lopRequestNeeded;           /**< LOP request should be sent (managed by CanOpenMenagerT) */
    TickType_t lastLopRequestTime;   /**< Timestamp of last LOP request attempt */
    uint8_t lopRequestAttempts;      /**< Number of LOP request attempts made */
    bool liftMapReceived;            /**< Flag: LIFT_MASK SDO response received */
    bool doorMapReceived;            /**< Flag: DOOR_MASK SDO response received */

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

/** @brief Mutex for protecting canOpenNodesList from race conditions */
extern SemaphoreHandle_t canOpenNodesListMutex;

/* ============================================================================ */
/* FUNCTION PROTOTYPES                                                        */
/* ============================================================================ */

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

/**
 * @brief Send CANopen master heartbeat message
 * 
 * Transmits a heartbeat message from the master node (node ID 1) indicating it is alive
 * and operational. Should be called periodically (100-500ms intervals).
 * 
 * Heartbeat COB-ID: 0x701 (0x700 + master node ID 1)
 * 
 * @see CiA 301 - Heartbeat Protocol Specification
 */
void CANOPEN_SendMasterHeartbeat(void);

/**
 * @brief FDCAN RX FIFO0 interrupt callback handler for CANopen messages
 * 
 * Processes received CAN messages from FDCAN RX FIFO0. Called from interrupt
 * context when a message is received on FDCAN1. Extracts message data and queues it
 * for processing in task context.
 * 
 * @param hfdcan Pointer to FDCAN handle
 * @param RxFifo0ITs Flags indicating which RX FIFO0 interrupt(s) occurred
 * @return None
 * @note Called from ISR context - must be fast and reentrant-safe
 */
void canOpenRxFifo0Callback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo0ITs);

/**
 * @brief FDCAN RX FIFO1 interrupt callback handler for CANopen messages
 * 
 * Processes received CAN messages from FDCAN RX FIFO1. Called from interrupt
 * context when a message is received on FDCAN1. Extracts message data and queues it
 * for processing in task context.
 * 
 * @param hfdcan Pointer to FDCAN handle
 * @param RxFifo1ITs Flags indicating which RX FIFO1 interrupt(s) occurred
 * @return None
 * @note Called from ISR context - must be fast and reentrant-safe
 */
void canOpenRxFifo1Callback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo1ITs);

#endif /* CAN_OPEN_MANAGER_H */
