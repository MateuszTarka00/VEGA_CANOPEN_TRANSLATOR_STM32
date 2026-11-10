#include "canOpenLopDefinitions.h"
#include "protocolUtils.h"
#include "vegaCanDefinitions.h"
#include "vegaCanManager.h"
#include "task.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static CanOpenNodeObject source, destination;
static CAN_Message_t sent[128];
static unsigned int sentCount;
static TickType_t ticks;
static bool sendAccepted = true;
static unsigned int rejectAfter = 128U;
FDCAN_HandleTypeDef hfdcan1, hfdcan2;

TickType_t xTaskGetTickCount(void) { return ticks; }
uint32_t HAL_GetTick(void) { return ticks; }
void vTaskDelay(TickType_t delay) { (void)delay; }
void vPortEnterCritical(void) {}
void vPortExitCritical(void) {}
CanOpenNodeObject *getCanOpenObjectsList(void) { return &source; }
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(FDCAN_HandleTypeDef *handle)
{
    (void)handle;
    return 1U;
}
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *handle,
    FDCAN_TxHeaderTypeDef *header, uint8_t *data)
{
    assert(handle == &hfdcan1);
    if (!sendAccepted || sentCount >= rejectAfter) { return HAL_ERROR; }
    uint8_t length = DLC2LEN(header->DataLength);
    assert(sentCount < sizeof(sent) / sizeof(sent[0]));
    sent[sentCount].id = header->Identifier;
    sent[sentCount].len = length;
    memcpy(sent[sentCount].data, data, length);
    sentCount++;
    return HAL_OK;
}

static CAN_Message_t *lastSent(void)
{
    assert(sentCount != 0U);
    return &sent[sentCount - 1U];
}

static void receive(CanOpenNodeObject *node, const uint8_t *data)
{
    CAN_Message_t message = {0};
    message.id = 0x580U + node->canOpenNodeHandler.canOpenID;
    message.len = 8U;
    memcpy(message.data, data, 8U);
    decomposeCanOpenMessage(&node->canOpenNodeHandler, &message);
}

static void expectRead(uint8_t subIndex)
{
    LOP_PollFloorTexts();
    CAN_Message_t *message = lastSent();
    assert(message->id == 0x615U && message->len == 8U);
    assert(message->data[0] == 0x40U && message->data[1] == 0xB0U);
    assert(message->data[2] == 0x62U && message->data[3] == subIndex);
}

static void display(uint8_t floor)
{
    destination.canOpenNodeHandler.displayedFloor = floor;
    destination.canOpenNodeHandler.changeFlags = DISPLAYED_FLOOR;
    processNodeToSendMsg(&destination.canOpenNodeHandler);
    assert(lastSent()->id == MASTER_LOP_TX_ID);
    assert(lastSent()->data[1] == floor);
    LOP_PollFloorTexts();
    assert(lastSent()->id == 0x616U);
    assert(lastSent()->data[1] == 0x30U && lastSent()->data[2] == 0x65U);
    assert(lastSent()->data[3] == 2U);
}

static void testSpecialIndications(void)
{
    CanOpenNodeHandler node = {0};
    node.doorMap = 1U;
    node.floorNumber = 2U;
    node.liftMap = 4U;
    node.specialInformation = CANOPEN_SPECIAL_INDICATION_OVERLOAD;
    node.changeFlags = SPECIAL_INFORMATION;
    sendAccepted = false;
    processNodeToSendMsg(&node);
    assert(sentCount == 0U);
    assert(node.specialInformationPrev == CANOPEN_SPECIAL_INDICATION_NONE);
    assert(node.changeFlags & SPECIAL_INFORMATION);

    sendAccepted = true;
    processNodeToSendMsg(&node);
    const uint8_t overload[6] = {LOP_SPECIAL_INDICATION_ID,
        CANOPEN_SPECIAL_INDICATION_OVERLOAD, 4U, 2U, 1U, TRUE};
    assert(sentCount == 1U && lastSent()->id == MASTER_LOP_TX_ID);
    assert(lastSent()->len == sizeof(overload));
    assert(memcmp(lastSent()->data, overload, sizeof(overload)) == 0);
    assert(node.specialInformationPrev == node.specialInformation);
    assert(!(node.changeFlags & SPECIAL_INFORMATION));

    node.changeFlags = SPECIAL_INFORMATION;
    processNodeToSendMsg(&node);
    assert(sentCount == 1U && !(node.changeFlags & SPECIAL_INFORMATION));

    node.specialInformation = CANOPEN_SPECIAL_INDICATION_FIRE_ALARM;
    node.changeFlags = SPECIAL_INFORMATION;
    sendAccepted = false;
    processNodeToSendMsg(&node);
    assert(sentCount == 1U);
    assert(node.specialInformationPrev == CANOPEN_SPECIAL_INDICATION_OVERLOAD);
    assert(node.changeFlags & SPECIAL_INFORMATION);

    sendAccepted = true;
    rejectAfter = 2U;
    processNodeToSendMsg(&node);
    assert(sentCount == 2U);
    assert(lastSent()->data[INDICATOR_BYTE] == CANOPEN_SPECIAL_INDICATION_OVERLOAD);
    assert(lastSent()->data[ACTIVATION_BYTE] == FALSE);
    assert(node.specialInformationPrev == CANOPEN_SPECIAL_INDICATION_NONE);
    assert(node.changeFlags & SPECIAL_INFORMATION);

    rejectAfter = 128U;
    processNodeToSendMsg(&node);
    assert(sentCount == 3U);
    assert(lastSent()->data[INDICATOR_BYTE] == CANOPEN_SPECIAL_INDICATION_FIRE_ALARM);
    assert(lastSent()->data[ACTIVATION_BYTE] == TRUE);
    assert(node.specialInformationPrev == node.specialInformation);
    assert(!(node.changeFlags & SPECIAL_INFORMATION));

    node.specialInformation = CANOPEN_SPECIAL_INDICATION_NONE;
    node.changeFlags = SPECIAL_INFORMATION;
    processNodeToSendMsg(&node);
    assert(sentCount == 4U);
    assert(lastSent()->data[INDICATOR_BYTE] == CANOPEN_SPECIAL_INDICATION_FIRE_ALARM);
    assert(lastSent()->data[ACTIVATION_BYTE] == FALSE);
    assert(node.specialInformationPrev == CANOPEN_SPECIAL_INDICATION_NONE);
    assert(!(node.changeFlags & SPECIAL_INFORMATION));

    node.changeFlags = SPECIAL_INFORMATION;
    processNodeToSendMsg(&node);
    assert(sentCount == 4U && !(node.changeFlags & SPECIAL_INFORMATION));
    sentCount = 0U;
}

static bool validateVega(const CAN_Message_t *message)
{
    return validateMessage(message, VEGA_RX_MIN_MESSAGE_LENGTH,
        VEGA_RX_MAX_MESSAGE_LENGTH, FIRST_RECEIVE_ID,
        FIRST_RECEIVE_ID + VEGA_RX_FLOOR_COUNT);
}

static void testVegaSpecialInformation(void)
{
    const uint8_t selectors[] = {VEGA_SPECIAL_INFORMATION_ID_0,
        VEGA_SPECIAL_INFORMATION_ID_1, VEGA_SPECIAL_INFORMATION_ID_2};
    const uint8_t statuses[] = {VEGA_OUT_OF_SERVICE_ID, VEGA_INSPECTION_ID,
        VEGA_OVERLOAD_ID, VEGA_FIRE_ALARM_ID};
    const uint8_t indications[] = {CANOPEN_SPECIAL_INDICATION_NO_SERVICE,
        CANOPEN_SPECIAL_INDICATION_MAINTENANCE,
        CANOPEN_SPECIAL_INDICATION_OVERLOAD, CANOPEN_SPECIAL_INDICATION_FIRE_ALARM};
    CAN_Message_t message = {0};
    message.len = VEGA_RX_SPECIAL_MIN_MESSAGE_LENGTH;
    message.data[VEGA_RX_MESSAGE_ID_BYTE] = VEGA_RX_SPECIAL_MESSAGE_MARKER;
    source.nextObject = &destination;
    ticks = 100U;

    for (unsigned int selector = 0U; selector < sizeof(selectors); ++selector) {
        for (unsigned int status = 0U; status < sizeof(statuses); ++status) {
            message.data[VEGA_RX_SPECIAL_VALUE_BYTE] = selectors[selector];
            message.data[VEGA_RX_SPECIAL_FLOOR_BYTE] = statuses[status];
            assert(validateVega(&message));
            source.canOpenNodeHandler.changeFlags = 0U;
            destination.canOpenNodeHandler.changeFlags = 0U;
            processVegaMessage(&message);
            assert(source.canOpenNodeHandler.specialInformation == indications[status]);
            assert(destination.canOpenNodeHandler.specialInformation == indications[status]);
            assert(source.canOpenNodeHandler.changeFlags == SPECIAL_INFORMATION);
            assert(destination.canOpenNodeHandler.changeFlags == SPECIAL_INFORMATION);
            assert(source.canOpenNodeHandler.displayedFloor == 0U);
            assert(destination.canOpenNodeHandler.displayedFloor == 0U);
            source.canOpenNodeHandler.changeFlags = 0U;
            processVegaMessage(&message);
            assert(source.canOpenNodeHandler.changeFlags == 0U);
        }
    }

    message.data[VEGA_RX_SPECIAL_VALUE_BYTE] = 0U;
    assert(!validateVega(&message));
    processVegaMessage(&message);
    assert(source.canOpenNodeHandler.specialInformation == CANOPEN_SPECIAL_INDICATION_FIRE_ALARM);
    message.data[VEGA_RX_SPECIAL_VALUE_BYTE] = selectors[0];
    message.data[VEGA_RX_SPECIAL_FLOOR_BYTE] = 'Z';
    assert(!validateVega(&message));
    message.data[VEGA_RX_SPECIAL_FLOOR_BYTE] = VEGA_OVERLOAD_ID;
    message.len = VEGA_RX_SPECIAL_MIN_MESSAGE_LENGTH - 1U;
    assert(!validateVega(&message));
    message.len = VEGA_RX_SPECIAL_MIN_MESSAGE_LENGTH;

    ticks = 1000U;
    processVegaMessage(&message);
    ticks = 2100U;
    message.data[VEGA_RX_DISPLAY_TYPE_BYTE] = VEGA_DISPLAY_FLOOR_TYPE;
    message.data[VEGA_RX_SPECIAL_FLOOR_BYTE] = VEGA_DISPLAY_FLOOR_ASCII_BASE;
    message.data[VEGA_RX_SPECIAL_VALUE_BYTE] = VEGA_DISPLAY_UP_ARROW_CODE;
    assert(validateVega(&message));
    processVegaMessage(&message);
    assert(source.canOpenNodeHandler.displayedFloor == 1U);
    assert(destination.canOpenNodeHandler.displayedArrow == UP_ARROW);
    assert(source.canOpenNodeHandler.specialInformation == CANOPEN_SPECIAL_INDICATION_OVERLOAD);
    ++ticks;
    processVegaMessage(&message);
    assert(source.canOpenNodeHandler.specialInformation == CANOPEN_SPECIAL_INDICATION_NONE);
    assert(destination.canOpenNodeHandler.specialInformation == CANOPEN_SPECIAL_INDICATION_NONE);
    assert(source.canOpenNodeHandler.changeFlags & SPECIAL_INFORMATION);
    assert(destination.canOpenNodeHandler.changeFlags & SPECIAL_INFORMATION);

    message.data[VEGA_RX_SPECIAL_FLOOR_BYTE] = VEGA_DISPLAY_FLOOR_ASCII_BASE - 1U;
    assert(!validateVega(&message));
    message.data[VEGA_RX_SPECIAL_FLOOR_BYTE] = VEGA_DISPLAY_FLOOR_ASCII_LIMIT;
    assert(!validateVega(&message));
    message.data[VEGA_RX_SPECIAL_FLOOR_BYTE] = VEGA_DISPLAY_FLOOR_ASCII_BASE;
    message.data[VEGA_RX_DISPLAY_TYPE_BYTE] = 0U;
    assert(!validateVega(&message));

    ticks = UINT32_MAX - 100U;
    message.data[VEGA_RX_SPECIAL_FLOOR_BYTE] = VEGA_OVERLOAD_ID;
    message.data[VEGA_RX_SPECIAL_VALUE_BYTE] = selectors[0];
    processVegaMessage(&message);
    message.data[VEGA_RX_DISPLAY_TYPE_BYTE] = VEGA_DISPLAY_FLOOR_TYPE;
    message.data[VEGA_RX_SPECIAL_FLOOR_BYTE] = VEGA_DISPLAY_FLOOR_ASCII_BASE;
    message.data[VEGA_RX_SPECIAL_VALUE_BYTE] = VEGA_DISPLAY_DOWN_ARROW_CODE;
    ticks = 999U;
    processVegaMessage(&message);
    assert(source.canOpenNodeHandler.specialInformation == CANOPEN_SPECIAL_INDICATION_OVERLOAD);
    assert(destination.canOpenNodeHandler.displayedArrow == DOWN_ARROW);
    ticks = 1000U;
    processVegaMessage(&message);
    assert(source.canOpenNodeHandler.specialInformation == CANOPEN_SPECIAL_INDICATION_NONE);
    memset(&source, 0, sizeof(source));
    memset(&destination, 0, sizeof(destination));
    ticks = 0U;
}

int main(void)
{
    testSpecialIndications();
    testVegaSpecialInformation();
    source.canOpenNodeHandler.canOpenID = 21U;
    source.canOpenNodeHandler.displayedFloor = 255U;
    source.nextObject = &destination;
    destination.canOpenNodeHandler.canOpenID = 22U;
    destination.canOpenNodeHandler.displayedFloor = 255U;
    LOP_ResetFloorTexts(21U);
    LOP_PollFloorTexts();
    assert(sentCount == 0U);
    source.canOpenNodeHandler.heartbeatReceived = true;
    source.canOpenNodeHandler.lopRequestNeeded = true;
    LOP_PollFloorTexts();
    assert(sentCount == 0U);
    source.canOpenNodeHandler.lopRequestNeeded = false;
    expectRead(1U);
    uint8_t reply[8] = {0x4FU, 0xB0U, 0x62U, 1U, 'P', 0U, 0U, 0U};
    receive(&source, reply);

    expectRead(2U);
    uint8_t upload[8] = {0x41U, 0xB0U, 0x62U, 2U, 8U, 0U, 0U, 0U};
    receive(&source, upload);
    assert(lastSent()->data[0] == 0x60U);
    uint8_t firstSegment[8] = {0x00U, 'A', 'B', 'C', 'D', 'E', 'F', 'G'};
    receive(&source, firstSegment);
    assert(lastSent()->data[0] == 0x70U);
    uint8_t lastSegment[8] = {0x1DU, 'H', 0U, 0U, 0U, 0U, 0U, 0U};
    receive(&source, lastSegment);

    uint8_t missing[8] = {0x80U, 0xB0U, 0x62U, 3U, 0x11U, 0U, 9U, 6U};
    for (unsigned int attempt = 0U; attempt < 3U; ++attempt) {
        expectRead(3U);
        receive(&source, missing);
    }
    for (uint8_t subIndex = 4U; subIndex <= 24U; ++subIndex) {
        expectRead(subIndex);
        reply[3] = subIndex;
        receive(&source, reply);
    }
    unsigned int before = sentCount;
    LOP_PollFloorTexts();
    assert(sentCount == before);

    uint8_t downloadAck[8] = {0x60U, 0x30U, 0x65U, 2U, 0U, 0U, 0U, 0U};
    display(1U);
    assert(lastSent()->data[0] == 0x2FU && lastSent()->data[4] == 'P');
    receive(&destination, downloadAck);
    display(2U);
    assert(lastSent()->data[0] == 0x21U && lastSent()->data[4] == 8U);
    receive(&destination, downloadAck);
    assert(lastSent()->data[0] == 0x00U);
    assert(memcmp(&lastSent()->data[1], "ABCDEFG", 7U) == 0);
    uint8_t segmentAck[8] = {0x20U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    receive(&destination, segmentAck);
    assert(lastSent()->data[0] == 0x1DU && lastSent()->data[1] == 'H');
    segmentAck[0] = 0x30U;
    receive(&destination, segmentAck);
    display(3U);
    assert(lastSent()->data[0] == 0x2FU && lastSent()->data[4] == '3');
    receive(&destination, downloadAck);

    destination.canOpenNodeHandler.floorTextPending = true;
    sendAccepted = false;
    LOP_PollFloorTexts();
    assert(destination.canOpenNodeHandler.floorTextPending);
    sendAccepted = true;
    LOP_PollFloorTexts();
    receive(&destination, downloadAck);

    LOP_ResetFloorTexts(21U);
    expectRead(1U);
    ticks += pdMS_TO_TICKS(500U);
    LOP_PollFloorTexts();
    assert(lastSent()->data[0] == 0x80U);
    expectRead(1U);
    uint8_t badSize[8] = {0x41U, 0xB0U, 0x62U, 1U, 9U, 0U, 0U, 0U};
    receive(&source, badSize);
    assert(lastSent()->data[0] == 0x80U);
    expectRead(1U);
    reply[3] = 1U;
    reply[4] = 'R';
    receive(&source, reply);
    expectRead(2U);
    receive(&source, upload);
    firstSegment[0] = 0x10U;
    receive(&source, firstSegment);
    assert(lastSent()->data[0] == 0x80U);
    expectRead(2U);
    puts("LOP floor text and VEGA special indication tests passed");
    return 0;
}