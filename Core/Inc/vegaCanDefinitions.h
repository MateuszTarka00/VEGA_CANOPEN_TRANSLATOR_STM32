/*
 * vegaCanDefinitions.h
 *
 *  Created on: 18 maj 2026
 *      Author: mateo
 */

#ifndef INC_VEGACANDEFINITIONS_H_
#define INC_VEGACANDEFINITIONS_H_

#include "main.h"

#define REST_INPUT 							0
#define UP_INPUT   							1
#define DOWN_INPUT 							2
#define TWO_BUTTONS_INPUT 					3

#define SECOND_BYTE_VALUE					0x0F
#define FIRST_FLOOR_NUMBER_RX				0x80 //First RX byte

#define FIRST_FLOOR_NUMBER_TX				0x00
#define FIRST_FLOOR_NUMBER_ID				0x200

#define DOWN_BUTTON_THIRD_BYTE_TX			0x01
#define UP_BUTTON_THIRD_BYTE_TX				0x02
#define TWO_BUTTON_THIRD_BYTE_TX			0x03

#define DOWN_BUTTON_THIRD_BYTE_CONST_RX		0x01
#define UP_BUTTON_THIRD_BYTE_CONST_RX		0x02

#define DOWN_BUTTON_THIRD_BYTE_BLINK_RX		0x41
#define UP_BUTTON_THIRD_BYTE_BLINK_RX		0x82

extern const uint8_t inputCanLastByte[20][4];

#endif /* INC_VEGACANDEFINITIONS_H_ */
