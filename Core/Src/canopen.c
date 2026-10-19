/** @file canopen.c
 * @brief Portable local service dispatcher; dependency diagram in canopen.h.
 */
#include "canopen.h"
#include <string.h>

bool Co_Init(Canopen *c, uint8_t node, CoSend send, void *user,
              const PsData *identity, PsStore *store,
              const FiData *factory, const BootHandoffPlatform *boot, uint32_t now)
{
    if ((c == NULL) || (node == 0U) || (node > 127U) ||
        (send == NULL) || !Ps_Validate(identity)) { return false; }
    memset(c, 0, sizeof(*c)); c->node_id = node; c->send = send; c->user = user;
    Od_Init(&c->od, identity, store, factory, boot, node); CoSdo_Init(&c->sdo, &c->od, node);
    c->heartbeat_at = now;
    CoHeartbeat_Frame(&c->pending, node, 0U); c->tx_pending = true;
    return true;
}

bool Co_Receive(Canopen *c, const CoFrame *f, uint32_t now)
{
    if ((f->id == 0U) && (f->length == 2U) &&
        (f->data[0] == 0x81U) && (f->data[1] == c->node_id)) {
        if (!c->tx_pending && (c->sdo.state == SDO_IDLE) &&
            !BootHandoff_Busy(&c->od.handoff)) {
            uint32_t result = c->od.factory_valid ?
                BootHandoff_RequestDiscovery(&c->od.handoff, now) :
                CO_ABORT_HARDWARE;
            if (result != 0U) {
                (void)Co_Error(c, 0x5000U, 1U, result);
            }
        }
        return true;
    }
    if (f->id == (uint16_t)(0x600U + c->node_id)) {
        /* Owner must flush pending responses before accepting another request. */
        if (!c->tx_pending && (c->od.handoff.state != BOOT_WAIT_ACK) &&
            (c->od.handoff.state != BOOT_RESETTING)) {
            c->tx_pending = CoSdo_Receive(&c->sdo, f, now, &c->pending);
        }
        return true;
    }
    return false;
}

void Co_Poll(Canopen *c, uint32_t now)
{
    BootHandoffState before = c->od.handoff.state;
    BootHandoff_Poll(&c->od.handoff, now);
    if ((before == BOOT_WAIT_ACK) && (c->od.handoff.state == BOOT_FAILED)) {
        /* Never send a stale successful entry response after abandoning handoff. */
        if (c->tx_pending) {
            c->pending.data[0] = 0x80U;
            Co_Put16(&c->pending.data[1], 0x2000U); c->pending.data[3] = 0U;
            Co_Put32(&c->pending.data[4], CO_ABORT_HARDWARE);
        }
    }
    if ((c->od.handoff.state == BOOT_WAIT_ACK) && c->od.handoff.ack_queued) { return; }
    if (!c->tx_pending && CoSdo_Poll(&c->sdo, now, &c->pending)) {
        c->tx_pending = true;
    }
    if (!c->tx_pending && (c->od.identity.heartbeat_ms != 0U) &&
        ((now - c->heartbeat_at) >= c->od.identity.heartbeat_ms)) {
        CoHeartbeat_Frame(&c->pending, c->node_id, 5U); c->tx_pending = true;
    }
    if (c->tx_pending) {
        if (c->send(c->user, &c->pending)) {
            if ((c->od.handoff.state == BOOT_WAIT_ACK) &&
                (c->pending.id == (uint16_t)(0x580U + c->node_id)) &&
                ((c->pending.data[0] == 0x60U) ||
                 ((c->pending.data[0] & 0xEFU) == 0x20U))) {
                BootHandoff_AckQueued(&c->od.handoff);
            }
            if (c->pending.id == (uint16_t)(0x700U + c->node_id)) {
                c->heartbeat_at = now;
            }
            c->tx_pending = false;
        } else { c->tx_failures++; }
    }
}

bool Co_Error(Canopen *c, uint16_t code, uint8_t reg, uint32_t detail)
{
    if (c->tx_pending || (c->od.handoff.state == BOOT_WAIT_ACK) ||
        (c->od.handoff.state == BOOT_RESETTING)) { return false; }
    CoEmcy_Record(&c->od, &c->pending, c->node_id, code, reg, detail);
    c->tx_pending = true;
    return true;
}
