/**
 * @file protocolUtils.c
 * @brief Implementation of shared protocol utility functions
 * 
 * This module provides common functions used by both CANopen and VEGA protocol
 * handlers, reducing code duplication and improving maintainability.
 * 
 * All functions are designed for testability - they accept parameters instead
 * of accessing globals, enabling unit testing without hardware mocking.
 * 
 * Created: 2026-09-04
 * Refactoring: Code consolidation for reliability and testability
 */

#include "protocolUtils.h"
#include "canOpenManager.h"
#include "canOpenLopDefinitions.h"
#include "vegaCanDefinitions.h"
#include "fdcan.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ============================================================================
 * FDCAN UTILITY FUNCTIONS (SHARED)
 * ============================================================================
 */

/**
 * @brief Convert byte count to FDCAN Data Length Code (DLC)
 * 
 * Maps the number of data bytes to the appropriate FDCAN DLC value.
 * FDCAN supports 0-8 bytes of data in classic CAN format.
 * Shared utility used by both CANopen and VEGA protocol handlers.
 * 
 * @param len Number of data bytes (0-8)
 * @return FDCAN DLC constant value
 * @note Values >8 default to FDCAN_DLC_BYTES_8 for safety
 */
uint32_t FDCAN_BytesToDLC(uint8_t len)
{
    switch (len)
    {
        case 0: return FDCAN_DLC_BYTES_0;
        case 1: return FDCAN_DLC_BYTES_1;
        case 2: return FDCAN_DLC_BYTES_2;
        case 3: return FDCAN_DLC_BYTES_3;
        case 4: return FDCAN_DLC_BYTES_4;
        case 5: return FDCAN_DLC_BYTES_5;
        case 6: return FDCAN_DLC_BYTES_6;
        case 7: return FDCAN_DLC_BYTES_7;
        case 8: return FDCAN_DLC_BYTES_8;
        default: return FDCAN_DLC_BYTES_8; /* Safety fallback for invalid lengths */
    }
}

/* ============================================================================
 * FDCAN CALLBACK IMPLEMENTATIONS (HAL OVERRIDES)
 * ============================================================================
 */

/* Forward declarations for external FDCAN handles and queues */
extern FDCAN_HandleTypeDef hfdcan1;  /* CANopen interface */
extern FDCAN_HandleTypeDef hfdcan2;  /* VEGA interface */
extern QueueHandle_t canOpenRxQueue; /* CANopen message queue */
extern QueueHandle_t vegaRxQueue;    /* VEGA message queue */

/**
 * @brief Handle FDCAN RX FIFO0 message reception (HAL weak override)
 * 
 * This is a strong implementation of the weak HAL callback that overrides
 * the default HAL driver implementation. It routes received messages to the
 * appropriate protocol queue based on which FDCAN interface received the message.
 * 
 * - FDCAN1 (CANopen) messages → canOpenRxQueue
 * - FDCAN2 (VEGA) messages → vegaRxQueue
 * 
 * @param hfdcan Pointer to FDCAN_HandleTypeDef structure
 * @param RxFifo0ITs Bitmask of RX FIFO0 interrupt triggers
 * @note Called from ISR context, must use FromISR queue functions
 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    FDCAN_RxHeaderTypeDef rxHeader;
    CAN_Message_t msg = {0};
    BaseType_t hpw = pdFALSE;

    if (hfdcan == NULL) {
        return;
    }

    /* Retrieve message from FIFO0 */
    HAL_StatusTypeDef status = HAL_FDCAN_GetRxMessage(
        hfdcan,
        FDCAN_RX_FIFO0,
        &rxHeader,
        msg.data
    );

    if (status != HAL_OK) {
        return;
    }

    msg.id = rxHeader.Identifier;
    msg.len = DLC2LEN(rxHeader.DataLength);

    /* Route to appropriate queue based on FDCAN interface */
    if (hfdcan == &hfdcan1) {
        /* CANopen message - queue to canOpenRxQueue */
        xQueueSendFromISR(canOpenRxQueue, &msg, &hpw);
        portYIELD_FROM_ISR(hpw);
    } else if (hfdcan == &hfdcan2) {
        /* VEGA message - queue to vegaRxQueue */
        xQueueSendFromISR(vegaRxQueue, &msg, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
}

/**
 * @brief Handle FDCAN RX FIFO1 message reception (HAL weak override)
 * 
 * This is a strong implementation of the weak HAL callback that overrides
 * the default HAL driver implementation. It routes received messages to the
 * appropriate protocol queue based on which FDCAN interface received the message.
 * 
 * - FDCAN1 (CANopen) messages → canOpenRxQueue
 * - FDCAN2 (VEGA) messages → vegaRxQueue
 * 
 * @param hfdcan Pointer to FDCAN_HandleTypeDef structure
 * @param RxFifo1ITs Bitmask of RX FIFO1 interrupt triggers
 * @note Called from ISR context, must use FromISR queue functions
 */
void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo1ITs)
{
    FDCAN_RxHeaderTypeDef rxHeader;
    CAN_Message_t msg = {0};
    BaseType_t hpw = pdFALSE;

    if (hfdcan == NULL) {
        return;
    }

    /* Retrieve message from FIFO1 */
    HAL_StatusTypeDef status = HAL_FDCAN_GetRxMessage(
        hfdcan,
        FDCAN_RX_FIFO1,
        &rxHeader,
        msg.data
    );

    if (status != HAL_OK) {
        return;
    }

    msg.id = rxHeader.Identifier;
    msg.len = DLC2LEN(rxHeader.DataLength);

    /* Route to appropriate queue based on FDCAN interface */
    if (hfdcan == &hfdcan1) {
        /* CANopen message - queue to canOpenRxQueue */
        xQueueSendFromISR(canOpenRxQueue, &msg, &hpw);
        portYIELD_FROM_ISR(hpw);
    } else if (hfdcan == &hfdcan2) {
        /* VEGA message - queue to vegaRxQueue */
        xQueueSendFromISR(vegaRxQueue, &msg, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
}

/* ============================================================================
 * INPUT VALIDATION HELPERS
 * ============================================================================
 */

/**
 * @brief Validate CAN message parameters for processing
 * 
 * @param msg Pointer to CAN message to validate
 * @param minLen Minimum required data length
 * @param maxLen Maximum allowed data length
 * @param minId Minimum valid message ID (inclusive)
 * @param maxId Maximum valid message ID (exclusive)
 * 
 * @return true if message passes all validation checks, false otherwise
 */
bool validateMessage(
    const CAN_Message_t *msg,
    uint8_t minLen,
    uint8_t maxLen,
    uint32_t minId,
    uint32_t maxId
)
{
    /* Validate message pointer */
    if (msg == NULL) {
        return false;
    }

    /* Validate message length is within bounds */
    if (msg->len < minLen || msg->len > maxLen) {
        return false;
    }

    /* Validate message ID is within range */
    if (msg->id < minId || msg->id >= maxId) {
        return false;
    }

    return true;
}

/**
 * @brief Validate pointer is not NULL
 * 
 * @param ptr Pointer to check
 * @return true if pointer is valid (not NULL), false otherwise
 */
bool validatePointer(const void *ptr)
{
    return ptr != NULL;
}

/* ============================================================================
 * BUTTON STATE MANAGEMENT (TESTABLE)
 * ============================================================================
 */

/**
 * @brief Set button state in CANopen node with automatic clearing of other button
 * 
 * @param node Pointer to CANopen node to update
 * @param buttonType Button type (UP_BUTTON or DOWN_BUTTON from enum CallButtonsEnum)
 * @param isPressed true to press button, false to release
 * 
 * @return true if state was modified, false if no change or invalid input
 */
bool setButtonState(
    CanOpenNodeHandler *node,
    uint8_t buttonType,
    bool isPressed
)
{
    /* Validate input parameter */
    if (node == NULL) {
        return false;
    }

    /* Handle UP button */
    if (buttonType == UP_BUTTON) {
        node->upButtonState = isPressed;
        if (isPressed) {
            node->downButtonState = FALSE;  /* Clear DOWN button when UP pressed */
        }
        return true;
    }

    /* Handle DOWN button */
    if (buttonType == DOWN_BUTTON) {
        node->downButtonState = isPressed;
        if (isPressed) {
            node->upButtonState = FALSE;  /* Clear UP button when DOWN pressed */
        }
        return true;
    }

    /* Unknown button type */
    return false;
}

/**
 * @brief Set LED state in CANopen node
 * 
 * @param node Pointer to CANopen node to update
 * @param ledType LED type (UP_LED or DOWN_LED)
 * @param state LED state (FALSE for off, TRUE for on/blinking)
 * 
 * @return true if state was modified, false if no change or invalid input
 */
bool setLedState(
    CanOpenNodeHandler *node,
    uint8_t ledType,
    bool state
)
{
    /* Validate input parameter */
    if (node == NULL) {
        return false;
    }

    /* Handle DOWN LED */
    if (ledType == DOWN_LED_STATE) {
        node->downLedState = state;
        if (state) {
            node->upLedState = FALSE;  /* Clear UP LED when DOWN LED enabled */
        }
        /* Set changeFlags to trigger transmission */
        node->changeFlags |= DOWN_LED_STATE;
        return true;
    }

    /* Handle UP LED */
    if (ledType == UP_LED_STATE) {
        node->upLedState = state;
        if (state) {
            node->downLedState = FALSE;  /* Clear DOWN LED when UP LED enabled */
        }
        /* Set changeFlags to trigger transmission */
        node->changeFlags |= UP_LED_STATE;
        return true;
    }

    /* Unknown LED type */
    return false;
}

/* ============================================================================
 * NODE LIST TRAVERSAL HELPERS
 * ============================================================================
 */

/**
 * @brief Find CANopen node in linked list by node ID
 * 
 * @param nodeListHead Pointer to head of node linked list
 * @param targetNodeId CANopen node ID to search for
 * 
 * @return Pointer to matching CanOpenNodeObject if found, NULL otherwise
 */
CanOpenNodeObject* findNodeById(
    const CanOpenNodeObject *nodeListHead,
    uint32_t targetNodeId
)
{
    /* Traverse linked list */
    const CanOpenNodeObject *current = nodeListHead;
    
    while (current != NULL) {
        if (current->canOpenNodeHandler.canOpenID == targetNodeId) {
            /* Found matching node - cast away const for caller */
            return (CanOpenNodeObject*)current;
        }
        current = current->nextObject;
    }

    /* Node not found */
    return NULL;
}

/**
 * @brief Convert VEGA floor number to CANopen node ID
 * 
 * Mapping formula: nodeId = 20 + floorNumber
 * 
 * @param floorNumber VEGA floor number (0-19)
 * @return CANopen node ID (20-39) for this floor
 */
uint32_t floorToCanOpenId(uint8_t floorNumber)
{
    return FLOOR_TO_NODE_ID(floorNumber);
}

/* ============================================================================
 * MESSAGE EXTRACTION HELPERS (TESTABLE PROTOCOL PARSING)
 * ============================================================================
 */

/**
 * @brief Extract floor number from VEGA message ID
 * 
 * Encapsulates: floor = msg_id - FIRST_RECEIVE_ID (0x80)
 * 
 * @param vegaMessageId VEGA message ID (0x80 - 0x93)
 * @return Floor number (0-19) or 0xFF if invalid
 */
uint8_t extractFloorFromVegaId(uint32_t vegaMessageId)
{
    /* Validate message ID is in VEGA RX range */
    if (vegaMessageId < FIRST_RECEIVE_ID || vegaMessageId >= (FIRST_RECEIVE_ID + 20)) {
        return 0xFF;  /* Invalid - return marker value */
    }

    return (uint8_t)(vegaMessageId - FIRST_RECEIVE_ID);
}

/* ============================================================================
 * PROTOCOL-AWARE CAN MESSAGE TRANSMISSION
 * ============================================================================
 */

/**
 * @brief Send CAN message to appropriate interface based on protocol type
 * 
 * @param messageId CAN message identifier
 * @param data Pointer to message data bytes
 * @param length Number of data bytes
 * @param protocol Protocol type specifying target interface (PROTOCOL_CANOPEN or PROTOCOL_VEGA)
 * 
 * @return true if message queued successfully, false otherwise
 */
bool protocolSend(uint32_t messageId, const uint8_t *data, uint8_t length, ProtocolType_t protocol)
{
    FDCAN_TxHeaderTypeDef txHeader;
    FDCAN_HandleTypeDef *targetHandle;
    uint32_t timeoutTicks = 0;
    const uint32_t MAX_WAIT_TICKS = 100;  /* ~100ms timeout */

    /* Validate input parameters */
    if (data == NULL || length > 8) {
        return false;  /* Invalid parameters */
    }

    /* Select appropriate CAN interface based on protocol type */
    if (protocol == PROTOCOL_VEGA) {
        targetHandle = &hfdcan2;  /* VEGA uses FDCAN2 */
    } else {
        targetHandle = &hfdcan1;  /* CANopen uses FDCAN1 */
    }

    /* Configure CAN message header */
    txHeader.Identifier = messageId;
    txHeader.IdType = FDCAN_STANDARD_ID;
    txHeader.TxFrameType = FDCAN_DATA_FRAME;
    txHeader.DataLength = FDCAN_BytesToDLC(length);
    txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    txHeader.BitRateSwitch = FDCAN_BRS_OFF;
    txHeader.FDFormat = FDCAN_CLASSIC_CAN;
    txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    txHeader.MessageMarker = 0;

    /* Wait for TX FIFO space with timeout to prevent deadlock */
    while (HAL_FDCAN_GetTxFifoFreeLevel(targetHandle) == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));  /* Yield CPU */
        timeoutTicks++;
        if (timeoutTicks >= MAX_WAIT_TICKS) {
            return false;  /* Timeout - discard message */
        }
    }

    /* Queue message for transmission */
    HAL_FDCAN_AddMessageToTxFifoQ(targetHandle, &txHeader, (uint8_t *)data);
    
    return true;
}
