/**
 * @file bootloader_handoff.h
 * @brief Application-to-independent-bootloader request, NOT an image receiver.
 * Dependencies: handoff -> persistent_storage -> canopen_types.
 * Single task owner. No Flash image operations, allocation or direct vector jump.
 * @example BootHandoff_Init(&handoff, &platform, &store, &identity, 1U);
 */
#ifndef BOOTLOADER_HANDOFF_H
#define BOOTLOADER_HANDOFF_H
#include "persistent_storage.h"

#define BOOT_HANDOFF_PROTOCOL 2U
#define BOOT_REQUEST_NONE 0U
#define BOOT_REQUEST_UPDATE 1U
#define BOOT_REQUEST_DISCOVERY 2U

typedef enum {
    BOOT_IDLE = 0, BOOT_WAIT_ACK = 1, BOOT_RESETTING = 2,
    BOOT_FAILED = 4, BOOT_UNAVAILABLE = 6
} BootHandoffState;
typedef enum {
    BOOT_RESULT_NONE, BOOT_RESULT_OK, BOOT_RESULT_UNSUPPORTED,
    BOOT_RESULT_SERVICE_DENIED, BOOT_RESULT_STORAGE,
    BOOT_RESULT_TX_TIMEOUT, BOOT_RESULT_TX_FAILED,
    BOOT_RESULT_RESET_RETURNED, BOOT_RESULT_RECOVERY_REQUIRED
} BootHandoffResult;
typedef enum { BOOT_TX_PENDING, BOOT_TX_CONFIRMED, BOOT_TX_FAILED } BootTxStatus;
typedef enum {
    BOOT_STAGE_NONE, BOOT_STAGE_AVAILABLE, BOOT_STAGE_LOAD,
    BOOT_STAGE_JOURNAL, BOOT_STAGE_SERVICE, BOOT_STAGE_SAVE,
    BOOT_STAGE_WAIT_ACK, BOOT_STAGE_RESET, BOOT_STAGE_DISCOVERY
} BootHandoffStage;

/** Board implementation is mandatory before enabling handoff.
 * installed verifies the actual protected bootloader/layout/contract, not a flag.
 * enterService authorizes the outage and quiesces LOP/VEGA at an external interlock.
 * Each accepted service entry starts a fresh response-tracking transaction.
 * ackStatus refers ONLY to this request's final SDO reply, not FIFO emptiness.
 * reset must execute a system reset and must not return.
 * All callbacks run in the owner task. No default successful implementations.
 */
typedef struct {
    void *user;
    uint32_t bitrate;
    bool (*installed)(void *user);
    bool (*enterService)(void *user);
    void (*leaveService)(void *user);
    BootTxStatus (*ackStatus)(void *user);
    /** Retire/abort that response before returning from a failed transition.
     * true means it can no longer appear later on CAN. Must be idempotent. */
    bool (*cancelAck)(void *user);
    void (*reset)(void *user);
    /** Optional: bootloader explicitly advertises bounded NMT discovery. */
    bool (*discoveryInstalled)(void *user);
} BootHandoffPlatform;

typedef struct {
    BootHandoffPlatform platform;
    PsStore *store;
    PsData *identity;
    BootHandoffState state;
    BootHandoffResult result;
    BootHandoffStage stage;
    PsResult storage_result;
    uint32_t started_at, timeout_ms;
    uint8_t node_id;
    bool service, ack_queued;
} BootHandoff;

void BootHandoff_Init(BootHandoff *handoff, const BootHandoffPlatform *platform,
                       PsStore *store, PsData *identity, uint8_t node_id);
bool BootHandoff_Available(const BootHandoff *handoff);
uint32_t BootHandoff_Request(BootHandoff *handoff, uint32_t now);
/** Addressed NMT reset has no SDO ACK; reset only after durable service entry. */
uint32_t BootHandoff_RequestDiscovery(BootHandoff *handoff, uint32_t now);
/** Called only when the successful final response was accepted by the CAN driver. */
void BootHandoff_AckQueued(BootHandoff *handoff);
void BootHandoff_Poll(BootHandoff *handoff, uint32_t now);
/** Recover a failed transaction, clearing any durable request before leaving service. */
uint32_t BootHandoff_Recover(BootHandoff *handoff);
/** Blocks other OD writes/RX during the committed transition or unresolved failure. */
bool BootHandoff_Busy(const BootHandoff *handoff);
#endif
