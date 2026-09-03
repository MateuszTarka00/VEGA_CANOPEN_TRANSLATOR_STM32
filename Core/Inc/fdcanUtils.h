/*
 * fdcanUtils.h
 *
 * FDCAN Shared Utility Functions
 * 
 * Provides shared, reusable utilities for FDCAN (Flexible Data-rate CAN)
 * operations. Eliminates code duplication across protocol drivers.
 *
 * Created: 2026-09-03
 * Author: Refactoring effort
 */

#ifndef INC_FDCANUTILS_H_
#define INC_FDCANUTILS_H_

#include "main.h"

/**
 * @brief Convert byte count to FDCAN Data Length Code (DLC)
 * 
 * Maps the number of data bytes to the appropriate FDCAN DLC value.
 * FDCAN supports 0-8 bytes of data in classic CAN format.
 * 
 * @param len Number of data bytes (0-8)
 * @return FDCAN DLC constant value
 * @note Values >8 default to FDCAN_DLC_BYTES_8 for safety
 */
static inline uint32_t FDCAN_BytesToDLC(uint8_t len)
{
    switch (len)
    {
        case 0: return FDCAN_DLC_BYTES_0;
        case 1: return FDCAN_DLC_BYTES_1;
        case 2: return FDCAN_DLC_BYTES_2;
        case 3: return FDCAN_DLC_BYTES_3;
        case 4: return FDCAN_DLC_BYTES_4;
        case 5: return FDCAN_DLC_BYTES_5;
        case 6: return FDCAN_DLC_BYTES_6;
        case 7: return FDCAN_DLC_BYTES_7;
        case 8: return FDCAN_DLC_BYTES_8;
        default: return FDCAN_DLC_BYTES_8; /* Safety fallback for invalid lengths */
    }
}

#endif /* INC_FDCANUTILS_H_ */
