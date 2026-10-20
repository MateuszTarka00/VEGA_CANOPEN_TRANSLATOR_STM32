#include "canOpenLopDefinitions.h"
#include "protocolUtils.h"
#include "task.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static CanOpenNodeObject source, destination;
static CAN_Message_t sent[128];
static unsigned int sentCount;
static TickType_t ticks;
static bool sendAccepted = true;

TickType_t xTaskGetTickCount(void) { return ticks; }
void vPortEnterCritical(void) {}
void vPortExitCritical(void) {}
CanOpenNodeObject *getCanOpenObjectsList(void) { return &source; }
bool validatePointer(const void *pointer) { return pointer != NULL; }
bool setButtonState(CanOpenNodeHandler *node, uint8_t button, bool pressed)
{
    (void)node; (void)button; (void)pressed;
    return false;
}
bool protocolSend(uint32_t id, const uint8_t *data, uint8_t length, ProtocolType_t protocol)
{
    assert(protocol == PROTOCOL_CANOPEN);
    if (!sendAccepted) { return false; }
    assert(sentCount < sizeof(sent) / sizeof(sent[0]));
    sent[sentCount].id = id;
    sent[sentCount].len = length;
    memcpy(sent[sentCount].data, data, length);
    sentCount++;
    return true;
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

int main(void)
{
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
    puts("LOP floor text tests passed");
    return 0;
}