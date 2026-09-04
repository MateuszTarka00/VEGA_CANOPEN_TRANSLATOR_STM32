/**
 * @file protocolUtils.h
 * @brief Shared utility functions for CANopen and VEGA protocol handlers
 * 
 * This module provides common utilities to eliminate code duplication across
 * protocol handlers and improve testability through dependency injection.
 * 
 * Features:
 * - Input validation helpers (reduce duplicate checks)
 * - Message parameter validation
 * - Button and LED state management (testable, no global state)
 * - Node list traversal helpers
 * 
 * Design for testability:
 * - Functions accept system context as parameter
 * - No direct global state access
 * - Return results instead of side effects
 * 
 * Created: 2026-09-04
 * Refactoring: Code consolidation for reliability and testability
 */

#ifndef PROTOCOL_UTILS_H_
#define PROTOCOL_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "canOpenManager.h"

/* ============================================================================
 * PROTOCOL TYPE ENUMERATION
 * ============================================================================
 */

/**
 * @brief Protocol type for CAN message transmission routing
 * 
 * Used to explicitly specify which CAN interface and protocol should handle
 * the message transmission, avoiding ambiguity from ID-based detection.
 */
typedef enum
{
    PROTOCOL_CANOPEN = 0,  /**< CANopen protocol - routes to FDCAN1 */
    PROTOCOL_VEGA = 1      /**< VEGA protocol - routes to FDCAN2 */
} ProtocolType_t;

/* ============================================================================
 * FDCAN UTILITY FUNCTIONS (SHARED)
 * ============================================================================
 */

/**
 * @brief Convert byte count to FDCAN Data Length Code (DLC)
 * 
 * Shared utility used by both CANopen and VEGA protocol handlers.
 * Maps the number of data bytes to the appropriate FDCAN DLC value.
 * 
 * @param len Number of data bytes (0-8)
 * @return FDCAN DLC constant value
 * @note Values >8 default to FDCAN_DLC_BYTES_8 for safety
 */
uint32_t FDCAN_BytesToDLC(uint8_t len);

/* ============================================================================
 * INPUT VALIDATION HELPERS
 * ============================================================================
 */

/**
 * @brief Validate CAN message parameters for processing
 * 
 * Performs all common validation checks that multiple protocol handlers need:
 * - Null pointer check
 * - Message length bounds (1-8 bytes)
 * - Message ID range check
 * 
 * @param msg Pointer to CAN message to validate
 * @param minLen Minimum required data length
 * @param maxLen Maximum allowed data length
 * @param minId Minimum valid message ID (inclusive)
 * @param maxId Maximum valid message ID (exclusive)
 * 
 * @return true if message passes all validation checks, false otherwise
 * 
 * @note This function enables code sharing across CANopen and VEGA handlers
 */
bool validateMessage(
    const CAN_Message_t *msg,
    uint8_t minLen,
    uint8_t maxLen,
    uint32_t minId,
    uint32_t maxId
);

/**
 * @brief Validate pointer is not NULL
 * 
 * @param ptr Pointer to check
 * @return true if pointer is valid (not NULL), false otherwise
 */
bool validatePointer(const void *ptr);

/* ============================================================================
 * BUTTON STATE MANAGEMENT (TESTABLE)
 * ============================================================================
 */

/**
 * @brief Set button state in CANopen node with automatic clearing of other button
 * 
 * When one button is pressed (UP or DOWN), this function:
 * 1. Sets the pressed button state
 * 2. Clears the other button state automatically
 * 3. Sets appropriate changeFlags for transmission
 * 
 * This eliminates duplicate logic in multiple handlers.
 * 
 * @param node Pointer to CANopen node to update
 * @param buttonType Button type (UP_BUTTON or DOWN_BUTTON from enum CallButtonsEnum)
 * @param isPressed true to press button, false to release
 * 
 * @return true if state was modified, false if no change or invalid input
 * 
 * @note This function is testable - accepts node pointer as parameter, no global state
 * @note TESTABLE: Can be unit tested with mock CanOpenNodeHandler struct
 */
bool setButtonState(
    CanOpenNodeHandler *node,
    uint8_t buttonType,
    bool isPressed
);

/**
 * @brief Set LED state in CANopen node
 * 
 * Updates LED indicator state and sets changeFlags for transmission.
 * Supports both constant states and blinking states.
 * 
 * @param node Pointer to CANopen node to update
 * @param ledType LED type (UP_LED or DOWN_LED)
 * @param state LED state (FALSE for off, TRUE for on/blinking)
 * 
 * @return true if state was modified, false if no change or invalid input
 * 
 * @note This function is testable - no global state access
 * @note TESTABLE: Can be unit tested with mock CanOpenNodeHandler struct
 */
bool setLedState(
    CanOpenNodeHandler *node,
    uint8_t ledType,
    bool state
);

/* ============================================================================
 * NODE LIST TRAVERSAL HELPERS
 * ============================================================================
 */

/**
 * @brief Find CANopen node in linked list by node ID
 * 
 * Searches the node list for a node matching the given CANopen ID.
 * Eliminates duplicate traversal logic across multiple modules.
 * 
 * @param nodeListHead Pointer to head of node linked list
 * @param targetNodeId CANopen node ID to search for (typically 20-39 for floors)
 * 
 * @return Pointer to matching CanOpenNodeObject if found, NULL otherwise
 * 
 * @note Thread-safety: Caller must ensure list is not modified during traversal
 * @note This is a pure function - can be unit tested easily
 */
CanOpenNodeObject* findNodeById(
    const CanOpenNodeObject *nodeListHead,
    uint32_t targetNodeId
);

/**
 * @brief Convert VEGA floor number to CANopen node ID
 * 
 * Encapsulates the mapping formula: nodeId = 20 + floorNumber
 * Eliminates need to reference formula in multiple places.
 * 
 * @param floorNumber VEGA floor number (0-19)
 * @return CANopen node ID (20-39) for this floor
 * 
 * @note Thread-safe: Pure function with no side effects
 * @note TESTABLE: Simple pure function
 */
uint32_t floorToCanOpenId(uint8_t floorNumber);

/* ============================================================================
 * MESSAGE EXTRACTION HELPERS (TESTABLE PROTOCOL PARSING)
 * ============================================================================
 */

/**
 * @brief Extract floor number from VEGA message ID
 * 
 * Encapsulates: floor = msg_id - FIRST_RECEIVE_ID (0x80)
 * Simplifies VEGA RX handler and eliminates magic numbers.
 * 
 * @param vegaMessageId VEGA message ID (0x80 - 0x93)
 * @return Floor number (0-19) or 0xFF if invalid
 * 
 * @note Thread-safe: Pure function
 * @note TESTABLE: Simple extraction function
 */
uint8_t extractFloorFromVegaId(uint32_t vegaMessageId);

/* ============================================================================
 * PROTOCOL-AWARE CAN MESSAGE TRANSMISSION
 * ============================================================================
 */

/**
 * @brief Send CAN message to appropriate interface based on protocol type
 * 
 * Routes messages to the correct CAN interface based on explicit protocol type parameter:
 * - PROTOCOL_VEGA → FDCAN2 (VEGA protocol)
 * - PROTOCOL_CANOPEN → FDCAN1 (CANopen protocol)
 * 
 * Validates input, handles timeouts, and provides single unified entry point
 * for all protocol transmission.
 * 
 * @param messageId CAN message identifier
 * @param data Pointer to message data bytes (NULL check performed)
 * @param length Number of data bytes (0-8, validates bounds)
 * @param protocol Protocol type specifying target interface (PROTOCOL_CANOPEN or PROTOCOL_VEGA)
 * 
 * @return true if message queued successfully, false if validation failed or timeout
 * 
 * @note This function replaces direct FDCAN_Send() calls throughout the codebase
 * @note Caller must explicitly specify protocol (no ID-based detection to avoid ambiguity)
 * @note Validates all parameters; returns false on invalid input
 * @note Uses 100ms timeout to prevent task starvation
 */
bool protocolSend(uint32_t messageId, const uint8_t *data, uint8_t length, ProtocolType_t protocol);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_UTILS_H_ */
