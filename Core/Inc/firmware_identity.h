/**
 * @file firmware_identity.h
 * @brief Build identity, immutable through CANopen. No NVM dependency.
 * @example OD 0x100A reports APP_SW_VERSION_TEXT of the running image.
 */
#ifndef FIRMWARE_IDENTITY_H
#define FIRMWARE_IDENTITY_H
#include <stdint.h>
#define APP_MODULE_NAME "VEGA CANopen gateway"
#define APP_SW_VERSION_MAJOR 0
#define APP_SW_VERSION_MINOR 1
#define APP_SW_VERSION_PATCH 1
#define APP_STRINGIFY_INNER(value) #value
#define APP_STRINGIFY(value) APP_STRINGIFY_INNER(value)
#define APP_SW_VERSION_TEXT APP_STRINGIFY(APP_SW_VERSION_MAJOR) "." \
    APP_STRINGIFY(APP_SW_VERSION_MINOR) "." APP_STRINGIFY(APP_SW_VERSION_PATCH)
/* Vendor format: major[31:16], minor[15:8], patch[7:0]. */
#define APP_SW_VERSION_NUMBER (((uint32_t)APP_SW_VERSION_MAJOR << 16U) | \
    ((uint32_t)APP_SW_VERSION_MINOR << 8U) | (uint32_t)APP_SW_VERSION_PATCH)
#if (APP_SW_VERSION_MAJOR < 0) || (APP_SW_VERSION_MAJOR > 65535) || \
    (APP_SW_VERSION_MINOR < 0) || (APP_SW_VERSION_MINOR > 255) || \
    (APP_SW_VERSION_PATCH < 0) || (APP_SW_VERSION_PATCH > 255)
#error Invalid firmware version components
#endif
#endif
