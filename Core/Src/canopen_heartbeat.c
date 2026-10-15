/** @file canopen_heartbeat.c @brief Heartbeat wire helpers. */
#include "canopen_heartbeat.h"
#include <string.h>
void CoHeartbeat_Frame(CoFrame *f, uint8_t node, uint8_t state)
{
    memset(f, 0, sizeof(*f)); f->id = (uint16_t)(0x700U + node);
    f->length = 1U; f->data[0] = state;
}
bool CoHeartbeat_Valid(const CoFrame *f)
{
    return (f->id >= 0x701U) && (f->id <= 0x77FU) && (f->length == 1U) &&
        ((f->data[0] == 0U) || (f->data[0] == 4U) ||
         (f->data[0] == 5U) || (f->data[0] == 127U));
}
