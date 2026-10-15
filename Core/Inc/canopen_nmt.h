/**
 * @file canopen_nmt.h
 * @brief NMT master command encoder. Dependencies: canopen_types.
 * @example CoNmt_Command(&frame, 1U, 21U); // start remote LOP
 */
#ifndef CANOPEN_NMT_H
#define CANOPEN_NMT_H
#include "canopen_types.h"
bool CoNmt_Command(CoFrame *frame, uint8_t command, uint8_t target);
#endif
