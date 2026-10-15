/**
 * @file object_dictionary.h
 * @brief Local gateway OD. object_dictionary -> bootloader_handoff -> storage.
 * Application exposes identity and boot request, NOT image download objects.
 * No CiA 417 object numbers are invented or claimed compliant.
 * @example uint8_t data[128]; uint32_t size; Od_Read(&od, 0x1008, 0, data, &size);
 */
#ifndef OBJECT_DICTIONARY_H
#define OBJECT_DICTIONARY_H
#include "bootloader_handoff.h"
#include "factory_identity.h"
#include "firmware_identity.h"
#define OD_BUFFER_SIZE 128U
#define OD_ERROR_COUNT 8U
typedef struct {
    PsData identity;
    FiData factory;
    char hardware_text[FI_TEXT_SIZE];
    bool factory_valid;
    PsStore *store;
    BootHandoff handoff;
    uint32_t device_type;
    uint32_t errors[OD_ERROR_COUNT];
    uint8_t error_count, error_register, node_id;
} ObjectDictionary;

void Od_Init(ObjectDictionary *od, const PsData *identity, PsStore *store,
             const FiData *factory, const BootHandoffPlatform *boot, uint8_t node_id);
/** Caller provides OD_BUFFER_SIZE bytes. Strings exclude the trailing NUL. */
uint32_t Od_Read(ObjectDictionary *od, uint16_t index, uint8_t sub,
                 uint8_t *data, uint32_t *size);
/** Writes are applied only after a complete SDO download. */
uint32_t Od_Write(ObjectDictionary *od, uint16_t index, uint8_t sub,
                  const uint8_t *data, uint32_t size, uint32_t now);
uint32_t Od_CheckDownload(ObjectDictionary *od, uint16_t index, uint8_t sub,
                          uint32_t size, bool size_known);
#endif
