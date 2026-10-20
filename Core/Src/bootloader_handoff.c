/** @file bootloader_handoff.c
 * @brief Non-Flash reset marker -> exact SDO TX confirmation -> system reset.
 * Failed transitions retire their marker before resuming application operation.
 */
#include "bootloader_handoff.h"
#include <string.h>

bool BootHandoff_Available(const BootHandoff *h)
{
    /* The reset marker carries mode only; the target contract fixes the bus rate. */
    return (h->store != NULL) && (h->platform.bitrate == UINT32_C(250000)) &&
        (h->platform.installed != NULL) && (h->platform.enterService != NULL) &&
        (h->platform.leaveService != NULL) && (h->platform.ackStatus != NULL) &&
        (h->platform.cancelAck != NULL) && (h->platform.reset != NULL) &&
        (h->platform.prepareReset != NULL) && (h->platform.clearReset != NULL) &&
        h->platform.installed(h->platform.user);
}

void BootHandoff_Init(BootHandoff *h, const BootHandoffPlatform *platform,
                       PsStore *store, PsData *identity, uint8_t node)
{
    memset(h, 0, sizeof(*h));
    if (platform != NULL) { h->platform = *platform; }
    h->store = store; h->identity = identity; h->node_id = node;
    h->timeout_ms = 2000U;
    h->state = BootHandoff_Available(h) ? BOOT_IDLE : BOOT_UNAVAILABLE;
    h->result = (h->state == BOOT_UNAVAILABLE) ? BOOT_RESULT_UNSUPPORTED : BOOT_RESULT_NONE;
    if (identity->boot_request != BOOT_REQUEST_NONE) {
        h->state = BOOT_FAILED; h->result = BOOT_RESULT_RECOVERY_REQUIRED;
    }
}

bool BootHandoff_Busy(const BootHandoff *h)
{
    return h->service || (h->state == BOOT_WAIT_ACK) || (h->state == BOOT_RESETTING) ||
        (h->result == BOOT_RESULT_RECOVERY_REQUIRED);
}

static bool clearRequest(BootHandoff *h)
{
    return (h->identity->boot_request == BOOT_REQUEST_NONE) &&
        (h->platform.clearReset != NULL) &&
        h->platform.clearReset(h->platform.user);
}

static void fail(BootHandoff *h, BootHandoffResult result)
{
    h->state = BOOT_FAILED; h->result = result;
    if (h->ack_queued && !h->platform.cancelAck(h->platform.user)) {
        h->result = BOOT_RESULT_RECOVERY_REQUIRED; return;
    }
    h->ack_queued = false;
    if (!clearRequest(h)) {
        h->result = BOOT_RESULT_RECOVERY_REQUIRED;
        return; /* Do not release the service interlock with an uncertain request. */
    }
    if (h->service) {
        h->platform.leaveService(h->platform.user); h->service = false;
    }
}

static uint32_t request(BootHandoff *h, uint32_t now, uint32_t mode)
{
    PsData candidate;
    if (BootHandoff_Busy(h)) { return CO_ABORT_STATE; }
    h->stage = BOOT_STAGE_AVAILABLE;
    h->storage_result = PS_OK;
    if (!BootHandoff_Available(h)) {
        h->state = BOOT_UNAVAILABLE; h->result = BOOT_RESULT_UNSUPPORTED;
        return CO_ABORT_STATE;
    }
    /* Never manufacture boot decisions from uncommissioned/corrupt defaults. */
    h->stage = BOOT_STAGE_LOAD;
    h->storage_result = Ps_Load(h->store, &candidate);
    if (h->storage_result != PS_OK) {
        h->state = BOOT_FAILED; h->result = BOOT_RESULT_STORAGE;
        return CO_ABORT_HARDWARE;
    }
    h->stage = BOOT_STAGE_JOURNAL;
    if (candidate.boot_request != BOOT_REQUEST_NONE) {
        h->state = BOOT_FAILED; h->result = BOOT_RESULT_RECOVERY_REQUIRED;
        return CO_ABORT_STATE;
    }
    if ((mode == BOOT_REQUEST_DISCOVERY) &&
        (candidate.update_state != 0U) &&
        (candidate.update_state != UINT32_C(0x501))) {
        h->state = BOOT_FAILED; h->result = BOOT_RESULT_SERVICE_DENIED;
        return CO_ABORT_STATE;
    }
    h->stage = BOOT_STAGE_SERVICE;
    if (!h->platform.enterService(h->platform.user)) {
        h->state = BOOT_FAILED; h->result = BOOT_RESULT_SERVICE_DENIED;
        return CO_ABORT_STATE;
    }
    h->service = true;
    h->stage = BOOT_STAGE_SAVE;
    if (!h->platform.prepareReset(h->platform.user, mode)) {
        fail(h, BOOT_RESULT_MARKER); return CO_ABORT_HARDWARE;
    }
    *h->identity = candidate;
    h->state = BOOT_WAIT_ACK; h->result = BOOT_RESULT_NONE;
    h->stage = BOOT_STAGE_WAIT_ACK;
    h->started_at = now; h->ack_queued = false;
    return 0U;
}

uint32_t BootHandoff_Request(BootHandoff *h, uint32_t now)
{
    return request(h, now, BOOT_REQUEST_UPDATE);
}

uint32_t BootHandoff_RequestDiscovery(BootHandoff *h, uint32_t now)
{
    uint32_t result;
    if (BootHandoff_Busy(h)) { return CO_ABORT_STATE; }
    h->stage = BOOT_STAGE_DISCOVERY;
    h->storage_result = PS_OK;
    if ((h->platform.discoveryInstalled == NULL) ||
        !h->platform.discoveryInstalled(h->platform.user)) {
        h->result = BOOT_RESULT_UNSUPPORTED;
        return CO_ABORT_STATE;
    }
    result = request(h, now, BOOT_REQUEST_DISCOVERY);
    if (result == 0U) {
        h->state = BOOT_RESETTING;
        h->stage = BOOT_STAGE_RESET;
        h->result = BOOT_RESULT_OK;
        h->platform.reset(h->platform.user);
        fail(h, BOOT_RESULT_RESET_RETURNED);
        return CO_ABORT_HARDWARE;
    }
    return result;
}

void BootHandoff_AckQueued(BootHandoff *h)
{
    if (h->state == BOOT_WAIT_ACK) { h->ack_queued = true; }
}

void BootHandoff_Poll(BootHandoff *h, uint32_t now)
{
    if (h->state == BOOT_WAIT_ACK) {
        BootTxStatus tx = h->ack_queued ? h->platform.ackStatus(h->platform.user) : BOOT_TX_PENDING;
        if (tx == BOOT_TX_CONFIRMED) {
            h->state = BOOT_RESETTING; h->result = BOOT_RESULT_OK;
            h->stage = BOOT_STAGE_RESET;
            h->platform.reset(h->platform.user);
            fail(h, BOOT_RESULT_RESET_RETURNED);
        } else if (tx == BOOT_TX_FAILED) { fail(h, BOOT_RESULT_TX_FAILED); }
        else if ((now - h->started_at) >= h->timeout_ms) { fail(h, BOOT_RESULT_TX_TIMEOUT); }
    }
}

uint32_t BootHandoff_Recover(BootHandoff *h)
{
    if (h->state != BOOT_FAILED) { return CO_ABORT_STATE; }
    if (h->ack_queued && !h->platform.cancelAck(h->platform.user)) {
        h->result = BOOT_RESULT_RECOVERY_REQUIRED; return CO_ABORT_HARDWARE;
    }
    h->ack_queued = false;
    if (!clearRequest(h)) {
        h->result = BOOT_RESULT_RECOVERY_REQUIRED; return CO_ABORT_HARDWARE;
    }
    if (h->service) {
        h->platform.leaveService(h->platform.user); h->service = false;
    }
    h->state = BootHandoff_Available(h) ? BOOT_IDLE : BOOT_UNAVAILABLE;
    h->result = (h->state == BOOT_IDLE) ? BOOT_RESULT_NONE : BOOT_RESULT_UNSUPPORTED;
    return 0U;
}
