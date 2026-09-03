/*
 * canOpenLopDefinitions.h
 *
 * CANopen Lift Operation Protocol (LOP) Definitions
 * 
 * Defines protocol constants, message structures, and function prototypes
 * for the Lift Operation Protocol layer that bridges CANopen with
 * elevator-specific control messages.
 *
 * Created on: 23 sie 2026
 *      Author: mateo
 * Refactored: 2026-09-03 - Enhanced documentation and struct comments
 */

#ifndef INC_CANOPENLOPDEFINITIONS_H_
#define INC_CANOPENLOPDEFINITIONS_H_

#include "main.h"
#include "canOpenManager.h"

/* ============================================================================ */
/* LOP CAN MESSAGE ID MAPPINGS                                               */
/* ============================================================================ */

/** @brief Master TX ID base for lift operation messages */
#define MASTER_LOP_TX_ID 0x400

/** @brief Button press RX ID base for lift call messages */
#define BUTTON_LOP_RX_ID 0x480

/** @brief Calculate RX message ID from node ID */
#define CAN_OPEN_RX_ID(NODE_ID) (0x480 + NODE_ID)

/** @brief Convert floor number to CANopen node ID */
#define FLOOR_TO_NODE_ID(floor) (20 + floor)

/** @brief Convert CANopen node ID to floor number */
#define ID_TO_FLOOR_NUMBER(id) (id - 20)

/** @brief Master TX ID for lift/door configuration (SDO) */
#define MASTER_LOP_TX_LIFT_DOOR 0x600

/** @brief Master RX ID for lift/door configuration (SDO response) */
#define MASTER_LOP_RX_LIFT_DOOR 0x580

/* ============================================================================ */
/* LOP FUNCTION IDs & PROTOCOL CONSTANTS                                     */
/* ============================================================================ */

/** @brief Lift call function (button press) ID */
#define LOP_LIFT_CALL_FUNCTION_ID  0x02

/** @brief Floor display function ID */
#define LOP_LIFT_DISPLAY_FLOOR_ID  0x40

/** @brief Arrow display function ID */
#define LOP_LIFT_DISPLAY_ARROW_ID  0x42

/** @brief SDO read transfer function ID (TX) */
#define LOP_SDO_READ_TX            0x40

/** @brief SDO read response mask for unused bytes */
#define LOP_SDO_READ_RX_MASK       0x0C

/** @brief SDO read response function ID (RX) */
#define LOP_SDO_READ_RX            (0x4F & (~0x0C))

/* ============================================================================ */
/* MESSAGE BYTE POSITION INDICES                                              */
/* ============================================================================ */

/** @brief Function identifier byte in message */
#define FUNCTION_BYTE              0

/** @brief Indicator byte (LED, display value, etc.) */
#define INDICATOR_BYTE             1

/** @brief Lift number/map byte */
#define LIFT_NUMBER_BYTE           2

/** @brief Floor number byte */
#define FLOOR_NUMBER_BYTE          3

/** @brief Door configuration byte */
#define DOOR_NUMBER_BYTE           4

/** @brief Activation/enable byte */
#define ACTIVATION_BYTE            5

/* ============================================================================ */
/* OBJECT DICTIONARY ADDRESSES (Little Endian)                              */
/* ============================================================================ */

/** @brief OD address for lift mask (0x6001) */
#define LIFT_MASK                  0x0160

/** @brief OD address for floor number (0x6002) */
#define FLOOR_NUMBER               0x0260

/** @brief OD address for door mask (0x6003) */
#define DOOR_MASK                  0x0360

/* ============================================================================ */
/* ENUMERATED TYPES                                                           */
/* ============================================================================ */

/**
 * @brief Lift call button states
 */
typedef enum
{
    NO_BUTTON = 0,     /**< No button pressed */
    UP_BUTTON = 1,     /**< UP button pressed */
    DOWN_BUTTON = 2,   /**< DOWN button pressed */
    BOTH_BUTTONS = 3   /**< Both buttons pressed simultaneously */
} CallButtonsEnum;

/**
 * @brief Arrow indicator display modes
 */
typedef enum
{
    NO_ARROW = 0x00,   /**< No arrow displayed */
    UP_ARROW = 0x10,   /**< UP arrow displayed */
    DOWN_ARROW = 0x20, /**< DOWN arrow displayed */
    BOTH_ARROWS = 0x30 /**< Both arrows displayed (clear/disable state) */
} ArrowsEnum;

/* ============================================================================ */
/* MESSAGE UNION DEFINITIONS - PDO FORMAT (6 bytes)                           */
/* ============================================================================ */

/**
 * @brief Floor display message transmission format
 * 
 * Transmits the current floor number to be displayed on elevator panels.
 * Used to inform all nodes of current elevator location.
 */
typedef union
{
    struct
    {
        uint8_t function;          /**< LOP_LIFT_DISPLAY_FLOOR_ID */
        uint8_t floorIndicator;    /**< Floor number to display (0-99) */
        uint8_t liftMap;           /**< Available lifts bitmask */
        uint8_t floorNumber;       /**< Current floor in building */
        uint8_t doorMap;           /**< Door configuration bitmask */
        uint8_t onOff;             /**< Enable/disable display (TRUE/FALSE) */
    };

    uint8_t data[6];               /**< Raw message data array */

} FloorDisplayMessageTx;

/**
 * @brief Arrow display message transmission format
 * 
 * Transmits up/down arrow indicators showing elevator direction.
 * Used to show movement direction to waiting passengers.
 */
typedef union
{
    struct
    {
        uint8_t function;          /**< LOP_LIFT_DISPLAY_ARROW_ID */
        uint8_t arrowIndicator;    /**< Arrow value (ArrowsEnum) */
        uint8_t liftMap;           /**< Available lifts bitmask */
        uint8_t floorNumber;       /**< Current floor in building */
        uint8_t doorMap;           /**< Door configuration bitmask */
        uint8_t onOff;             /**< Enable/disable arrows (TRUE/FALSE) */
    };

    uint8_t data[6];               /**< Raw message data array */

} ArrowDisplayMessageTx;

/**
 * @brief LED indicator message transmission format
 * 
 * Transmits button LED indicator states (up/down call buttons).
 * Used to show which floor calls are pending.
 */
typedef union
{
    struct
    {
        uint8_t function;          /**< LOP_LIFT_CALL_FUNCTION_ID */
        uint8_t ledIndicator;      /**< LED type (UP_BUTTON/DOWN_BUTTON) */
        uint8_t liftMap;           /**< Available lifts bitmask */
        uint8_t floorNumber;       /**< Current floor in building */
        uint8_t doorMap;           /**< Door configuration bitmask */
        uint8_t onOff;             /**< LED on/off state (TRUE/FALSE) */
    };

    uint8_t data[6];               /**< Raw message data array */

} LedIndicatorMessageTx;

/**
 * @brief Arrival sound message transmission format
 * 
 * Transmits arrival notification (sound/chime) control.
 * Used to alert passengers when elevator arrives at floor.
 */
typedef union
{
    struct
    {
        uint8_t function;          /**< LOP function ID */
        uint8_t soundIndicator;    /**< Sound type or ID */
        uint8_t liftMap;           /**< Available lifts bitmask */
        uint8_t floorNumber;       /**< Current floor in building */
        uint8_t doorMap;           /**< Door configuration bitmask */
        uint8_t onOff;             /**< Sound on/off (TRUE/FALSE) */
    };

    uint8_t data[6];               /**< Raw message data array */

} ArrivingMessageTx;

/**
 * @brief Car call (button press) message transmission format
 * 
 * Transmits lift call button press status.
 * Format used for both RX button presses and TX acknowledgment.
 */
typedef union
{
    struct
    {
        uint8_t function;          /**< LOP_LIFT_CALL_FUNCTION_ID */
        uint8_t button;            /**< Button state (CallButtonsEnum) */
        uint8_t liftMap;           /**< Available lifts bitmask */
        uint8_t floorNumber;       /**< Current floor in building */
        uint8_t doorMap;           /**< Door configuration bitmask */
        uint8_t onOff;             /**< Always TRUE for call messages */
    };

    uint8_t data[6];               /**< Raw message data array */

} CarCallMessageTx;

/* ============================================================================ */
/* MESSAGE UNION DEFINITIONS - SDO FORMAT (8 bytes)                           */
/* ============================================================================ */

/**
 * @brief SDO (Service Data Object) message format for device configuration
 * 
 * Used for request/response of device configuration via CANopen Object Dictionary.
 * Enables reading/writing device parameters like lift masks, floor numbers, etc.
 */
typedef union
{
    struct __attribute__((packed))
    {
        uint8_t  command;          /**< SDO command specifier byte */
        uint16_t index;            /**< Object Dictionary index (little endian) */
        uint8_t  subIndex;         /**< Object Dictionary sub-index */
        uint8_t  returnedData[4];  /**< Response data (4 bytes max) */
    };

    uint8_t data[8];               /**< Raw message data array */

} sdoRxTx;

/* ============================================================================ */
/* FUNCTION PROTOTYPES                                                        */
/* ============================================================================ */

/**
 * @brief Decompose received CANopen message and update node state
 * 
 * Parses message data and updates the associated node's state information.
 * Handles button presses, device configuration responses, etc.
 * 
 * @param node Pointer to CANopen node handler to update
 * @param msg Pointer to received CAN message
 */
void decomposeCanOpenMessage(CanOpenNodeHandler *node, CAN_Message_t *msg);

/**
 * @brief Compose and transmit messages based on node state changes
 * 
 * Examines changeFlags and sends appropriate CANopen messages for any
 * pending state changes (LED updates, floor display, arrows, etc.).
 * 
 * @param node Pointer to CANopen node handler with pending changes
 */
void processNodeToSendMsg(CanOpenNodeHandler *node);

/**
 * @brief Request device configuration via CANopen SDO protocol
 * 
 * Issues SDO read requests to obtain device configuration data.
 * Responses are processed by decomposeCanOpenMessage().
 * 
 * @param node Pointer to CANopen node to configure
 */
void LOP_RequestAssignment(CanOpenNodeHandler *node);

#endif /* INC_CANOPENLOPDEFINITIONS_H_ */
