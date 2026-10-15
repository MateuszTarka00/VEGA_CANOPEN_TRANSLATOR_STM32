/** @file canopen_emcy.c @brief EMCY encoder and CiA 301 0x1003 history. */
#include "canopen_emcy.h"
#include <string.h>
void CoEmcy_Record(ObjectDictionary *o, CoFrame *f, uint8_t node,
                    uint16_t error, uint8_t reg, uint32_t detail)
{
    uint8_t i;
    o->error_register = reg;
    if (error != 0U) {
        for (i = OD_ERROR_COUNT - 1U; i > 0U; --i) { o->errors[i] = o->errors[i - 1U]; }
        o->errors[0] = (uint32_t)error | ((detail & 0xFFFFU) << 16U);
        if (o->error_count < OD_ERROR_COUNT) { o->error_count++; }
    }
    memset(f, 0, sizeof(*f)); f->id = (uint16_t)(0x80U + node); f->length = 8U;
    Co_Put16(f->data, error); f->data[2] = reg;
    Co_Put32(&f->data[3], detail);
}
