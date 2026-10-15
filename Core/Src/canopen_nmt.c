/** @file canopen_nmt.c @brief CiA 301 NMT master frame construction. */
#include "canopen_nmt.h"
#include <string.h>
bool CoNmt_Command(CoFrame *f, uint8_t command, uint8_t target)
{
    if ((target > 127U) || ((command != 1U) && (command != 2U) &&
        (command != 128U) && (command != 129U) && (command != 130U))) {
        return false;
    }
    memset(f, 0, sizeof(*f)); f->length = 2U;
    f->data[0] = command; f->data[1] = target;
    return true;
}
