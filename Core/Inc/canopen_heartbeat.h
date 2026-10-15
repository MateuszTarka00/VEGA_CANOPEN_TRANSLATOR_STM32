/**
 * @file canopen_heartbeat.h
 * @brief Heartbeat producer and wire validation, independent of HAL/RTOS.
 * Dependencies: canopen_types. Consumer deadlines remain in the legacy manager.
 * @example CoHeartbeat_Frame(&frame, 1U, 5U);
 */
#ifndef CANOPEN_HEARTBEAT_H
#define CANOPEN_HEARTBEAT_H
#include "canopen_types.h"
void CoHeartbeat_Frame(CoFrame *frame, uint8_t node, uint8_t state);
bool CoHeartbeat_Valid(const CoFrame *frame);
#endif
