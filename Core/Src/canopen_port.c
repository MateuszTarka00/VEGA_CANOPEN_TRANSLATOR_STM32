/** @file canopen_port.c
 * @brief Single-owner STM32 integration and explicit-update bootloader handoff.
 * Factory page is read-only; configuration/update journal is loaded separately.
 */
#include "canopen_port.h"
#include "canopen.h"
#include "flash.h"
#include "boot_reset_request.h"
#include <string.h>

#define APP_BOOT_TX_TRIAL UINT32_C(0x500)
#define APP_BOOT_TX_CONFIRMED UINT32_C(0x501)

static Canopen gateway;
static PsStore storage;
static PsResult storage_result;
static FiResult factory_result;
static uint32_t reported_drops, reported_errors;
static uint32_t reported_timeouts[4];
static uint8_t timeout_scan = 1U;
static bool storage_reported;
static bool factory_reported;
static uint32_t handoff_tx_buffer;
static uint8_t handoff_report;
static uint32_t handoff_flash_sr, handoff_flash_stage, handoff_flash_status;

static bool bootInstalled(void *user)
{
    const volatile uint32_t *contract = (const volatile uint32_t *)DEVICE_BOOT_CONTRACT;
    const volatile uint32_t *vectors = (const volatile uint32_t *)UINT32_C(0x08000000);
    static const uint32_t expected[8] = {
        UINT32_C(0x424C4331), 2U, DEVICE_SLOT_A_START, DEVICE_SLOT_SIZE,
        DEVICE_FACTORY_START, DEVICE_NVM_START, DEVICE_BOOT_SIZE, 1U
    };
    uint32_t i;
    (void)user;
    if ((vectors[0] < UINT32_C(0x20000000)) ||
        (vectors[0] > UINT32_C(0x20024000)) || ((vectors[0] & 7U) != 0U) ||
        ((vectors[1] & 1U) == 0U) ||
        (vectors[1] < UINT32_C(0x08000001)) ||
        (vectors[1] >= UINT32_C(0x08008FE0))) {
        return false;
    }
    for (i = 0U; i < 7U; ++i) {
        if (contract[i] != expected[i]) { return false; }
    }
    return (contract[7] & (1U | BOOT_RESET_CAPABILITY)) ==
        (1U | BOOT_RESET_CAPABILITY);
}

static bool discoveryInstalled(void *user)
{
    const volatile uint32_t *contract = (const volatile uint32_t *)DEVICE_BOOT_CONTRACT;
    return bootInstalled(user) && ((contract[7] & 2U) != 0U);
}

static bool enterUpdate(void *user)
{
    (void)user;
    if (CanopenPort_TrialPending()) { return false; }
    handoff_tx_buffer = 0U;
    return App_EnterUpdateService();
}

static void leaveUpdate(void *user)
{
    (void)user;
    App_LeaveUpdateService();
}

static BootTxStatus updateAckStatus(void *user)
{
    (void)user;
    if (handoff_tx_buffer == 0U) { return BOOT_TX_PENDING; }
    if ((hfdcan1.Instance->TXBTO & handoff_tx_buffer) != 0U) {
        return BOOT_TX_CONFIRMED;
    }
    if ((hfdcan1.Instance->TXBCF & handoff_tx_buffer) != 0U) {
        return BOOT_TX_FAILED;
    }
    return BOOT_TX_PENDING;
}

static bool cancelUpdateAck(void *user)
{
    uint32_t started = HAL_GetTick();
    (void)user;
    if (handoff_tx_buffer == 0U) { return true; }
    if (HAL_FDCAN_AbortTxRequest(&hfdcan1, handoff_tx_buffer) != HAL_OK) {
        return false;
    }
    while (HAL_FDCAN_IsTxBufferMessagePending(&hfdcan1, handoff_tx_buffer) != 0U) {
        if ((HAL_GetTick() - started) >= 50U) { return false; }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
    handoff_tx_buffer = 0U;
    return true;
}

static void resetForUpdate(void *user)
{
    (void)user;
    /* Remove historical reset flags so the receiver can identify this reset. */
    RCC->CSR |= RCC_CSR_RMVF;
    __DSB();
    NVIC_SystemReset();
}

static bool prepareReset(void *user, uint32_t mode)
{
    (void)user;
    return BootReset_Set(mode);
}

static bool clearReset(void *user)
{
    (void)user;
    return BootReset_Clear();
}

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
    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &header, (uint8_t *)f->data) != HAL_OK) {
        return false;
    }
    if (gateway.od.handoff.state == BOOT_WAIT_ACK) {
        handoff_tx_buffer = HAL_FDCAN_GetLatestTxFifoQRequestBuffer(&hfdcan1);
    }
    return true;
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
    BootHandoffPlatform boot = {
        NULL, 250000U, bootInstalled, enterUpdate, leaveUpdate,
        updateAckStatus, cancelUpdateAck, resetForUpdate, discoveryInstalled,
        prepareReset, clearReset
    };
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

bool CanopenPort_TrialPending(void)
{
    return gateway.od.identity.update_state == APP_BOOT_TX_TRIAL;
}

bool CanopenPort_ConfirmTrial(void)
{
    PsData candidate;
    if (!CanopenPort_TrialPending()) {
        return true;
    }
    if ((Ps_Load(&storage, &candidate) != PS_OK) ||
        (candidate.update_state != APP_BOOT_TX_TRIAL) ||
        (candidate.pending_slot != 1U) ||
        (candidate.pending_version == 0U)) {
        return false;
    }
    candidate.update_state = APP_BOOT_TX_CONFIRMED;
    candidate.active_version = candidate.pending_version;
    if (Ps_Save(&storage, &candidate) != PS_OK) {
        return false;
    }
    gateway.od.identity = candidate;
    storage_result = PS_OK;
    return true;
}

bool CanopenPort_Receive(const CAN_Message_t *m)
{
    CoFrame frame;
    if ((m->id > 0x7FFU) || (m->len > 8U)) { return true; }
    frame.id = (uint16_t)m->id; frame.length = m->len;
    memcpy(frame.data, m->data, sizeof(frame.data));
    {
        bool handled = Co_Receive(&gateway, &frame, nowMs());
        if ((frame.id == 0U) && (frame.length == 2U) &&
            (frame.data[0] == 0x81U) && (frame.data[1] == gateway.node_id) &&
            gateway.tx_pending && (gateway.pending.id == 0x81U) &&
            (gateway.pending.data[6] == 0xB1U)) {
            handoff_flash_sr = FLASH_Guard.sr;
            handoff_flash_stage = FLASH_Guard.stage;
            handoff_flash_status = (uint32_t)FLASH_Guard.status;
            handoff_report = 1U;
        }
        return handled;
    }
}

void CanopenPort_Poll(void)
{
    uint32_t dropped, errors;
    Co_Poll(&gateway, nowMs());
    if ((handoff_report == 1U) &&
        Co_Error(&gateway, 0x5002U, 1U, handoff_flash_sr)) {
        gateway.pending.data[7] = (uint8_t)handoff_flash_stage;
        handoff_report = 2U;
    } else if ((handoff_report == 2U) &&
               Co_Error(&gateway, 0x5003U, 1U, handoff_flash_status)) {
        handoff_report = 0U;
    }
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
