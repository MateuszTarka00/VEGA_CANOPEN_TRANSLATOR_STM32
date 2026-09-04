/*
 * vegaCanDefinitions.h
 *
 * VEGA Protocol Definitions and Constants
 * 
 * Defines protocol-level constants, message formats, and lookup tables
 * for the VEGA elevator communication protocol.
 *
 *  Created on: 18 maj 2026
 *      Author: mateo
 * Refactored: 2026-09-03 - Enhanced documentation and added comments
 */

#ifndef INC_VEGACANDEFINITIONS_H_
#define INC_VEGACANDEFINITIONS_H_

#include "main.h"

/* Boolean value definitions for compatibility */
#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

/* ============================================================================ */
/* VEGA INPUT STATE ENUMERATION                                              */
/* ============================================================================ */

/** @brief REST state - no button pressed */
#define REST_INPUT              0

/** @brief UP button pressed/requested */
#define UP_INPUT                1

/** @brief DOWN button pressed/requested */
#define DOWN_INPUT              2

/** @brief Both UP and DOWN buttons pressed simultaneously */
#define TWO_BUTTONS_INPUT       3

/* ============================================================================ */
/* VEGA MESSAGE FORMAT CONSTANTS                                             */
/* ============================================================================ */

/** @brief Second byte value in all VEGA messages (constant 0x0F) */
#define SECOND_BYTE_VALUE       0x0F

/** @brief Base value for first byte in RX messages (floor offset added) */
#define FIRST_FLOOR_NUMBER_RX   0x80

/** @brief Base CAN ID for RX messages (receives from panel) - global constant */
#define FIRST_RECEIVE_ID        0x80

/** @brief Base value for first byte in TX messages */
#define FIRST_FLOOR_NUMBER_TX   0x00

/** @brief Base CAN ID for TX messages - global constant */
#define FIRST_SEND_ID           0x200

/** @brief Base CAN ID for TX messages */
#define FIRST_FLOOR_NUMBER_ID   0x200

/* ============================================================================ */
/* VEGA TX MESSAGE BUTTON STATE BYTE VALUES                                  */
/* ============================================================================ */

/** @brief Third byte value for DOWN button TX message */
#define DOWN_BUTTON_THIRD_BYTE_TX       0x01

/** @brief Third byte value for UP button TX message */
#define UP_BUTTON_THIRD_BYTE_TX         0x02

/** @brief Third byte value for both buttons pressed TX message */
#define TWO_BUTTON_THIRD_BYTE_TX        0x03

/* ============================================================================ */
/* VEGA RX MESSAGE BUTTON STATE BYTE VALUES                                  */
/* ============================================================================ */

/** @brief Third byte value for DOWN button RX message (constant state) */
#define DOWN_BUTTON_THIRD_BYTE_CONST_RX 0x01

/** @brief Third byte value for UP button RX message (constant state) */
#define UP_BUTTON_THIRD_BYTE_CONST_RX   0x02

/** @brief Third byte value for DOWN button RX message (blinking state) */
#define DOWN_BUTTON_THIRD_BYTE_BLINK_RX 0x41

/** @brief Third byte value for UP button RX message (blinking state) */
#define UP_BUTTON_THIRD_BYTE_BLINK_RX   0x82

/* ============================================================================ */
/* VEGA ARROW/DIRECTION INDICATORS                                           */
/* ============================================================================ */

/** @brief Lift moving upward - display UP arrow */
#define LIFT_GOING_UP           0x30

/** @brief Lift moving downward - display DOWN arrow */
#define LIFT_GOING_DOWN         0x31

/* ============================================================================ */
/* CHECKSUM LOOKUP TABLE                                                      */
/* ============================================================================ */

/**
 * @brief VEGA protocol checksum/last-byte lookup table
 * 
 * Provides the required fourth byte (checksum/CRC) for VEGA messages.
 * 
 * Format: inputCanLastByte[floor_number][state]
 * - floor_number: 0-19 (elevator floor)
 * - state: 0=REST (no button), 1=UP button, 2=DOWN button, 3=BOTH buttons
 * 
 * Each floor has 4 possible states, each with a corresponding checksum byte.
 * The checksum ensures message integrity in the VEGA protocol.
 */
extern const uint8_t inputCanLastByte[20][4];

#endif /* INC_VEGACANDEFINITIONS_H_ */
