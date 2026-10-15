/**
 * @file canopen.h
 * @brief Local services of the existing NMT master/LOP gateway.
 * Dependencies: canopen -> SDO/OD, heartbeat, EMCY; no HAL/RTOS.
 * One task owns this context and calls Poll/Receive. TX backpressure is explicit.
 * Before enabling boot handoff, the injected send/ackStatus callbacks must
 * correlate final SDO replies; CoSend success is only queue acceptance.
 * @example Co_Init(&co, 1U, send, user, &identity, &store, &factory, &boot, now);
 */
#ifndef CANOPEN_H
#define CANOPEN_H
#include "canopen_sdo.h"
#include "canopen_heartbeat.h"
#include "canopen_emcy.h"
#include "canopen_nmt.h"
typedef struct {
    ObjectDictionary od;
    CoSdo sdo;
    CoSend send;
    void *user;
    CoFrame pending;
    uint32_t heartbeat_at, tx_failures;
    uint8_t node_id;
    bool tx_pending;
} Canopen;
bool Co_Init(Canopen *co, uint8_t node, CoSend send, void *user,
              const PsData *identity, PsStore *store,
              const FiData *factory, const BootHandoffPlatform *boot, uint32_t now);
/** true: owned by the local service, do not forward to remote LOP discovery. */
bool Co_Receive(Canopen *co, const CoFrame *frame, uint32_t now);
void Co_Poll(Canopen *co, uint32_t now);
/** Records error only if there is room for its TX frame; caller retries otherwise. */
bool Co_Error(Canopen *co, uint16_t code, uint8_t reg, uint32_t detail);
#endif
