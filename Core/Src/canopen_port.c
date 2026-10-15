/** @file canopen_port.c
 * @brief Single-owner STM32 integration; bootloader handoff deliberately gated.
 * Factory page is read-only; configuration/update journal is loaded separately.
 */
#include "canopen_port.h"
#include "canopen.h"
#include "flash.h"
#include <string.h>

static Canopen gateway;
static PsStore storage;
static PsResult storage_result;
static FiResult factory_result;
static uint32_t reported_drops, reported_errors;
static uint32_t reported_timeouts[4];
static uint8_t timeout_scan = 1U;
static bool storage_reported;
static bool factory_reported;

static bool readPage(void *user, uint8_t page, uint32_t offset, uint8_t *data, uint32_t size)
{
    (void)user; return Flash_NvmRead(page, offset, data, size);
}
static bool erasePage(void *user, uint8_t page)
{
    (void)user; return Flash_NvmErase(page);
}
static bool writePage(void *user, uint8_t page, uint32_t offset,
                       const uint8_t *data, uint32_t size)
{
    (void)user; return Flash_NvmWrite(page, offset, data, size);
}
static bool sendFrame(void *user, const CoFrame *f)
{
    FDCAN_TxHeaderTypeDef header = {0};
    (void)user;
    if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0U) { return false; }
    header.Identifier = f->id; header.IdType = FDCAN_STANDARD_ID;
    header.TxFrameType = FDCAN_DATA_FRAME;
    header.DataLength = (uint32_t)f->length << 16U;
    header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    header.BitRateSwitch = FDCAN_BRS_OFF; header.FDFormat = FDCAN_CLASSIC_CAN;
    header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &header, (uint8_t *)f->data) == HAL_OK;
}
static uint32_t nowMs(void)
{
    return HAL_GetTick();
}

void CanopenPort_Init(void)
{
    PsData identity = {0};
    FiData factory;
    PsIo io = {NULL, readPage, erasePage, writePage};
    /* No installed bootloader/layout or service interlock has been validated.
     * Do not install reset/ACK callbacks until those board contracts exist. */
    BootHandoffPlatform boot = {0};
    Ps_Init(&storage, &io);
    storage_result = Ps_Load(&storage, &identity);
    if (storage_result != PS_OK) {
        /* Journal defaults never supply factory identity or running SW version. */
        memset(&identity, 0, sizeof(identity));
        identity.pending_slot = UINT32_MAX;
        identity.heartbeat_ms = 1000U;
    }
    factory_result = Flash_FactoryRead(&factory);
    if (!Co_Init(&gateway, 1U, sendFrame, NULL, &identity, &storage,
                 (factory_result == FI_OK) ? &factory : NULL, &boot, nowMs())) {
        Error_Handler();
    }
}

bool CanopenPort_Ready(void)
{
    return !gateway.tx_pending && (gateway.od.handoff.state != BOOT_WAIT_ACK) &&
        (gateway.od.handoff.state != BOOT_RESETTING);
}

bool CanopenPort_Receive(const CAN_Message_t *m)
{
    CoFrame frame;
    if ((m->id > 0x7FFU) || (m->len > 8U)) { return true; }
    frame.id = (uint16_t)m->id; frame.length = m->len;
    memcpy(frame.data, m->data, sizeof(frame.data));
    return Co_Receive(&gateway, &frame, nowMs());
}

void CanopenPort_Poll(void)
{
    uint32_t dropped, errors;
    Co_Poll(&gateway, nowMs());
    taskENTER_CRITICAL();
    dropped = canOpenRxDropped; errors = canOpenRxErrors;
    taskEXIT_CRITICAL();
    if (!factory_reported && (factory_result != FI_OK)) {
        if (Co_Error(&gateway, 0x5000U, 1U, UINT32_C(0x46490000) | (uint32_t)factory_result)) {
            factory_reported = true;
        }
    } else if (!storage_reported && (storage_result != PS_OK)) {
        if (Co_Error(&gateway, 0x5000U, 1U, (uint32_t)storage_result)) {
            storage_reported = true;
        }
    } else if ((dropped != reported_drops) || (errors != reported_errors)) {
        if (Co_Error(&gateway, 0x8110U, 0x11U, dropped)) {
            reported_drops = dropped; reported_errors = errors;
        }
    } else if (xSemaphoreTake(canOpenNodesListMutex, 0U) == pdTRUE) {
        CanOpenNodeObject *node = getCanOpenObjectsList();
        uint32_t bit = UINT32_C(1) << (timeout_scan % 32U);
        uint8_t word = timeout_scan / 32U;
        while ((node != NULL) && (node->canOpenNodeHandler.canOpenID != timeout_scan)) {
            node = node->nextObject;
        }
        if (node != NULL) {
            if (node->canOpenNodeHandler.heartbeatTimeoutError) {
                if (((reported_timeouts[word] & bit) == 0U) &&
                    Co_Error(&gateway, 0x8130U, 0x11U, timeout_scan)) {
                    reported_timeouts[word] |= bit;
                }
            } else { reported_timeouts[word] &= ~bit; }
        }
        xSemaphoreGive(canOpenNodesListMutex);
        timeout_scan = (timeout_scan == 127U) ? 1U : (uint8_t)(timeout_scan + 1U);
    }
}
