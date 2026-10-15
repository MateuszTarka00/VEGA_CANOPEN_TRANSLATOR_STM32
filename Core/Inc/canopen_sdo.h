/**
 * @file canopen_sdo.h
 * @brief Single-channel expedited/segmented SDO server.
 * Dependencies: SDO -> OD -> handoff/storage. Caller sends the returned frame.
 * @example if (CoSdo_Receive(&sdo, &request, now, &reply)) { send(&reply); }
 */
#ifndef CANOPEN_SDO_H
#define CANOPEN_SDO_H
#include "object_dictionary.h"
typedef enum { SDO_IDLE, SDO_UPLOAD, SDO_DOWNLOAD } CoSdoState;
typedef struct {
    ObjectDictionary *od;
    CoSdoState state;
    uint16_t index;
    uint8_t sub, node_id, toggle;
    uint8_t buffer[OD_BUFFER_SIZE];
    uint32_t size, offset, last_activity, timeout_ms;
    bool size_known;
} CoSdo;
void CoSdo_Init(CoSdo *sdo, ObjectDictionary *od, uint8_t node_id);
bool CoSdo_Receive(CoSdo *sdo, const CoFrame *request, uint32_t now, CoFrame *reply);
bool CoSdo_Poll(CoSdo *sdo, uint32_t now, CoFrame *reply);
void CoSdo_Reset(CoSdo *sdo);
#endif
