/** @file canopen_sdo.c
 * @brief CiA 301 SDO server, bounded object transfers, no image streaming.
 */
#include "canopen_sdo.h"
#include <string.h>

void CoSdo_Init(CoSdo *s, ObjectDictionary *od, uint8_t node)
{
    memset(s, 0, sizeof(*s)); s->od = od; s->node_id = node; s->timeout_ms = 1000U;
}

void CoSdo_Reset(CoSdo *s)
{
    s->state = SDO_IDLE;
}

static void response(CoSdo *s, CoFrame *r, uint8_t command)
{
    memset(r, 0, sizeof(*r));
    r->id = (uint16_t)(0x580U + s->node_id); r->length = 8U;
    r->data[0] = command;
}

static void abortTransfer(CoSdo *s, CoFrame *r, uint32_t code)
{
    response(s, r, 0x80U);
    Co_Put16(&r->data[1], s->index); r->data[3] = s->sub;
    Co_Put32(&r->data[4], code);
    CoSdo_Reset(s);
}

bool CoSdo_Receive(CoSdo *s, const CoFrame *q, uint32_t now, CoFrame *r)
{
    uint8_t cmd, count;
    uint32_t abort = 0U;
    if (q->id != (uint16_t)(0x600U + s->node_id)) { return false; }
    if (q->length != 8U) {
        abortTransfer(s, r, CO_ABORT_COMMAND); return true;
    }
    cmd = q->data[0];
    if (cmd == 0x80U) {
        if ((s->state != SDO_IDLE) && (Co_Get16(&q->data[1]) == s->index) &&
            (q->data[3] == s->sub)) { CoSdo_Reset(s); }
        return false;
    }
    s->last_activity = now;
    if (((cmd & 0xE0U) == 0x40U) || ((cmd & 0xE0U) == 0x20U)) {
        CoSdo_Reset(s);
        s->index = Co_Get16(&q->data[1]); s->sub = q->data[3];
        s->offset = 0U; s->toggle = 0U;
        if (cmd == 0x40U) {
            abort = Od_Read(s->od, s->index, s->sub, s->buffer, &s->size);
            if (abort == 0U) {
                if ((s->size > 0U) && (s->size <= 4U)) {
                    response(s, r, (uint8_t)(0x43U | ((4U - s->size) << 2U)));
                    memcpy(&r->data[4], s->buffer, s->size);
                } else {
                    response(s, r, 0x41U); Co_Put32(&r->data[4], s->size);
                    s->state = SDO_UPLOAD;
                }
                Co_Put16(&r->data[1], s->index); r->data[3] = s->sub;
            }
        } else if (((cmd & 0xE0U) == 0x20U) && ((cmd & 0x10U) == 0U)) {
            bool expedited = (cmd & 2U) != 0U;
            s->size_known = (cmd & 1U) != 0U;
            count = (uint8_t)((cmd >> 2U) & 3U);
            if ((!expedited && (count != 0U)) ||
                (expedited && !s->size_known && (count != 0U))) {
                abort = CO_ABORT_COMMAND;
            } else {
                s->size = expedited ? (s->size_known ? 4U - count : 4U) :
                    (s->size_known ? Co_Get32(&q->data[4]) : 0U);
                abort = Od_CheckDownload(s->od, s->index, s->sub, s->size,
                                         expedited || s->size_known);
                if (abort == 0U) {
                    if (expedited) {
                        abort = Od_Write(s->od, s->index, s->sub, &q->data[4], s->size, now);
                    } else { s->state = SDO_DOWNLOAD; }
                    response(s, r, 0x60U);
                    Co_Put16(&r->data[1], s->index); r->data[3] = s->sub;
                }
            }
        } else { abort = CO_ABORT_COMMAND; }
    } else if (((cmd & 0xEFU) == 0x60U) && (s->state == SDO_UPLOAD)) {
        uint32_t remaining = s->size - s->offset;
        bool last;
        if ((cmd & 0x10U) != s->toggle) { abort = CO_ABORT_TOGGLE; }
        else {
            count = (uint8_t)((remaining > 7U) ? 7U : remaining);
            last = remaining <= 7U;
            response(s, r, (uint8_t)(s->toggle | ((7U - count) << 1U) | (last ? 1U : 0U)));
            memcpy(&r->data[1], &s->buffer[s->offset], count);
            s->offset += count; s->toggle ^= 0x10U;
            if (last) { s->state = SDO_IDLE; }
        }
    } else if (((cmd & 0xE0U) == 0U) && (s->state == SDO_DOWNLOAD)) {
        bool last = (cmd & 1U) != 0U;
        count = (uint8_t)(7U - ((cmd >> 1U) & 7U));
        if ((cmd & 0x10U) != s->toggle) { abort = CO_ABORT_TOGGLE; }
        else if ((!last && (count != 7U)) ||
                 (s->size_known && ((s->offset > s->size) || (count > s->size - s->offset))) ||
                 (count > OD_BUFFER_SIZE - s->offset)) {
            abort = CO_ABORT_LENGTH;
        } else if (last && s->size_known && (s->offset + count != s->size)) {
            abort = CO_ABORT_LENGTH;
        } else {
            memcpy(&s->buffer[s->offset], &q->data[1], count);
            s->offset += count;
            if ((abort == 0U) && last) {
                abort = Od_Write(s->od, s->index, s->sub, s->buffer, s->offset, now);
            }
            if (abort == 0U) {
                response(s, r, (uint8_t)(0x20U | s->toggle)); s->toggle ^= 0x10U;
                if (last) { s->state = SDO_IDLE; }
            }
        }
    } else { abort = CO_ABORT_COMMAND; }
    if (abort != 0U) { abortTransfer(s, r, abort); }
    return true;
}

bool CoSdo_Poll(CoSdo *s, uint32_t now, CoFrame *r)
{
    if ((s->state != SDO_IDLE) && ((now - s->last_activity) >= s->timeout_ms)) {
        abortTransfer(s, r, CO_ABORT_TIMEOUT); return true;
    }
    return false;
}
