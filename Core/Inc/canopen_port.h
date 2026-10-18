/**
 * @file canopen_port.h
 * @brief STM32 gateway adapter. port -> CANopen, existing CAN manager, Flash/HAL.
 * Only CanOpenRxT may call this API. Legacy LOP/VEGA tasks do not own the OD.
 * @example CanopenPort_Init(); CanopenPort_Poll(); // in CanOpenRxT
 */
#ifndef CANOPEN_PORT_H
#define CANOPEN_PORT_H
#include "canOpenManager.h"
void CanopenPort_Init(void);
void CanopenPort_Poll(void);
bool CanopenPort_Ready(void);
bool CanopenPort_Receive(const CAN_Message_t *message);
/** True when the bootloader is waiting for the selected application trial. */
bool CanopenPort_TrialPending(void);
/** Persist application confirmation after its startup self-test has passed. */
bool CanopenPort_ConfirmTrial(void);
/** Cooperative task-boundary quiescence, used only for an explicit update. */
bool App_EnterUpdateService(void);
void App_LeaveUpdateService(void);
#endif
