/**
 * @file factory_identity_tool.c
 * @brief Host-only factory record generator. Never linked into device firmware.
 * Usage: factory_identity_tool vendor product revision serial variant hwMajor hwMinor output.bin
 * Numbers accept decimal or 0x hexadecimal. Values are assigned by manufacturing.
 */
#include "factory_identity.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static bool parse(const char *text, uint32_t maximum, uint32_t *value)
{
    char *end;
    unsigned long parsed;
    if ((text[0] == '\0') || (text[0] == '-') || (text[0] == '+') ||
        (text[0] < '0') || (text[0] > '9')) { return false; }
    errno = 0;
    parsed = strtoul(text, &end,
        ((text[0] == '0') && ((text[1] == 'x') || (text[1] == 'X'))) ? 16 : 10);
    if ((errno != 0) || (*end != '\0') || (parsed > maximum)) { return false; }
    *value = (uint32_t)parsed;
    return true;
}

int main(int argc, char **argv)
{
    uint32_t values[7], i;
    FiData data, verified;
    uint8_t record[FI_RECORD_SIZE];
    FILE *file;
    bool written, closed;
    if (argc != 9) {
        fputs("Usage: factory_identity_tool vendor product revision serial variant hwMajor hwMinor output.bin\n", stderr);
        return EXIT_FAILURE;
    }
    for (i = 0U; i < 7U; ++i) {
        if (!parse(argv[i + 1U], (i >= 5U) ? UINT16_MAX : UINT32_MAX, &values[i])) {
            fprintf(stderr, "Invalid numeric field %lu\n", (unsigned long)(i + 1U));
            return EXIT_FAILURE;
        }
    }
    data.vendor_id = values[0]; data.product_code = values[1];
    data.product_revision = values[2]; data.serial = values[3];
    data.hardware_variant = values[4];
    data.hardware_major = (uint16_t)values[5]; data.hardware_minor = (uint16_t)values[6];
    if ((Fi_Encode(&data, record, sizeof(record)) != FI_OK) ||
        (Fi_Decode(record, sizeof(record), &verified) != FI_OK)) {
        fputs("Factory identity violates schema validation\n", stderr);
        return EXIT_FAILURE;
    }
    /* Refuse an existing output in this single-process factory workflow. */
    file = fopen(argv[8], "rb");
    if (file != NULL) {
        (void)fclose(file); fputs("Output already exists\n", stderr); return EXIT_FAILURE;
    }
    if (errno != ENOENT) { perror("Cannot inspect output"); return EXIT_FAILURE; }
    file = fopen(argv[8], "wb");
    if (file == NULL) { perror("Cannot create output"); return EXIT_FAILURE; }
    written = fwrite(record, 1U, sizeof(record), file) == sizeof(record);
    closed = fclose(file) == 0;
    if (!written || !closed) {
        fputs("Factory record write failed; discard the incomplete output\n", stderr);
        return EXIT_FAILURE;
    }
    printf("Created %u-byte factory record for serial %lu at device address 0x%08lX\n",
           FI_RECORD_SIZE, (unsigned long)data.serial, (unsigned long)FI_FLASH_ADDRESS);
    return EXIT_SUCCESS;
}
