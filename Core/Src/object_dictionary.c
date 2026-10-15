/** @file object_dictionary.c
 * @brief Explicit little-endian OD access, no packed/native wire structures.
 */
#include "object_dictionary.h"
#include <string.h>

typedef char OdModuleNameFits[(sizeof(APP_MODULE_NAME) <= PS_STRING_SIZE) ? 1 : -1];
typedef char OdSoftwareVersionFits[(sizeof(APP_SW_VERSION_TEXT) <= PS_STRING_SIZE) ? 1 : -1];

void Od_Init(ObjectDictionary *o, const PsData *d, PsStore *s,
             const FiData *factory, const BootHandoffPlatform *b, uint8_t node)
{
    memset(o, 0, sizeof(*o));
    o->identity = *d; o->store = s; o->node_id = node;
    o->factory_valid = Fi_Validate(factory);
    if (o->factory_valid) {
        o->factory = *factory;
        o->factory_valid = Fi_HardwareText(factory, o->hardware_text, sizeof(o->hardware_text));
    }
    BootHandoff_Init(&o->handoff, b, s, &o->identity, node);
}

static uint32_t number(uint8_t *data, uint32_t *size, uint32_t v, uint32_t width)
{
    Co_Put32(data, v); *size = width; return 0U;
}

uint32_t Od_Read(ObjectDictionary *o, uint16_t index, uint8_t sub,
                 uint8_t *data, uint32_t *size)
{
    const char *text = NULL;
    uint32_t value = 0U, width = 4U;
    *size = 0U;
    switch (index) {
    case 0x1000U: value = o->device_type; break;
    case 0x1001U: value = o->error_register; width = 1U; break;
    case 0x1003U:
        if (sub == 0U) { value = o->error_count; width = 1U; }
        else if (sub <= o->error_count) { value = o->errors[sub - 1U]; }
        else { return CO_ABORT_SUBINDEX; }
        break;
    case 0x1008U: text = APP_MODULE_NAME; break;
    case 0x1009U: text = o->factory_valid ? o->hardware_text : "unconfigured"; break;
    case 0x100AU: text = APP_SW_VERSION_TEXT; break;
    case 0x1014U: value = 0x80U + o->node_id; break;
    case 0x1017U: value = o->identity.heartbeat_ms; width = 2U; break;
    case 0x1018U:
        switch (sub) {
        case 0U: value = 4U; width = 1U; break;
        case 1U: value = o->factory.vendor_id; break;
        case 2U: value = o->factory.product_code; break;
        case 3U: value = o->factory.product_revision; break;
        case 4U: value = o->factory.serial; break;
        default: return CO_ABORT_SUBINDEX;
        }
        break;
    case 0x1200U:
        switch (sub) {
        case 0U: value = 2U; width = 1U; break;
        case 1U: value = 0x600U + o->node_id; break;
        case 2U: value = 0x580U + o->node_id; break;
        default: return CO_ABORT_SUBINDEX;
        }
        break;
    case 0x2000U:
        return (sub == 0U) ? CO_ABORT_WRITE_ONLY : CO_ABORT_SUBINDEX;
    case 0x2001U: value = (uint32_t)o->handoff.state; width = 1U; break;
    case 0x2006U: value = APP_SW_VERSION_NUMBER; break;
    case 0x2007U: value = (uint32_t)o->handoff.result; width = 1U; break;
    case 0x2009U: value = BOOT_HANDOFF_PROTOCOL; break;
    case 0x200AU: value = 0U; width = 1U; break; /* Execution mode: application. */
    case 0x200BU:
        value = (o->factory_valid && BootHandoff_Available(&o->handoff)) ? 1U : 0U;
        width = 1U; break;
    default: return CO_ABORT_NOT_FOUND;
    }
    if ((sub != 0U) && (index != 0x1018U) && (index != 0x1003U) && (index != 0x1200U)) {
        return CO_ABORT_SUBINDEX;
    }
    if (text != NULL) {
        *size = (uint32_t)strlen(text);
        memcpy(data, text, *size);
        return 0U;
    }
    return number(data, size, value, width);
}

uint32_t Od_CheckDownload(ObjectDictionary *o, uint16_t index, uint8_t sub,
                          uint32_t size, bool known)
{
    uint32_t expected;
    switch (index) {
    case 0x1003U:
        if (sub != 0U) { return (sub <= o->error_count) ? CO_ABORT_READ_ONLY : CO_ABORT_SUBINDEX; }
        expected = 1U; break;
    case 0x1018U: return (sub <= 4U) ? CO_ABORT_READ_ONLY : CO_ABORT_SUBINDEX;
    case 0x1200U: return (sub <= 2U) ? CO_ABORT_READ_ONLY : CO_ABORT_SUBINDEX;
    case 0x2000U: expected = 1U; break;
    case 0x1017U: expected = 2U; break;
    case 0x1000U: case 0x1001U: case 0x1008U: case 0x1009U: case 0x100AU:
    case 0x1014U: case 0x2001U: case 0x2006U: case 0x2007U:
    case 0x2009U: case 0x200AU: case 0x200BU:
        return (sub == 0U) ? CO_ABORT_READ_ONLY : CO_ABORT_SUBINDEX;
    default: return CO_ABORT_NOT_FOUND;
    }
    if (sub != 0U) { return CO_ABORT_SUBINDEX; }
    if (known && (size != expected)) { return CO_ABORT_LENGTH; }
    return 0U;
}

uint32_t Od_Write(ObjectDictionary *o, uint16_t index, uint8_t sub,
                  const uint8_t *data, uint32_t size, uint32_t now)
{
    uint32_t abort;
    abort = Od_CheckDownload(o, index, sub, size, true);
    if (abort != 0U) { return abort; }
    if (BootHandoff_Busy(&o->handoff) &&
        !((index == 0x2000U) && (data[0] == 0U))) {
        return CO_ABORT_STATE;
    }
    if (index == 0x1003U) {
        if (data[0] != 0U) { return CO_ABORT_VALUE; }
        o->error_count = 0U; memset(o->errors, 0, sizeof(o->errors));
    } else if (index == 0x1017U) {
        PsData candidate = o->identity;
        candidate.heartbeat_ms = Co_Get16(data);
        if ((o->store == NULL) || (Ps_Save(o->store, &candidate) != PS_OK)) {
            return CO_ABORT_HARDWARE;
        }
        o->identity = candidate;
    } else if (index == 0x2000U) {
        switch (data[0]) {
        case 0U: return BootHandoff_Recover(&o->handoff);
        case 1U:
            if (!o->factory_valid) { return CO_ABORT_HARDWARE; }
            return BootHandoff_Request(&o->handoff, now);
        default: return CO_ABORT_VALUE;
        }
    }
    return 0U;
}
