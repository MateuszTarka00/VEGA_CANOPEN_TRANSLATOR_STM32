/**
 * @file canopen_emcy.h
 * @brief EMCY and newest-first error history. Dependencies: object_dictionary.
 * @example CoEmcy_Record(&od, &frame, 1U, 0x8130U, 0x10U, 21U);
 */
#ifndef CANOPEN_EMCY_H
#define CANOPEN_EMCY_H
#include "object_dictionary.h"
void CoEmcy_Record(ObjectDictionary *od, CoFrame *frame, uint8_t node,
                    uint16_t error, uint8_t error_register, uint32_t detail);
#endif
