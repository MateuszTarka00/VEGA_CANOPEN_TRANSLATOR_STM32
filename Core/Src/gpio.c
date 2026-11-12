/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"
#include <stdbool.h>

/* USER CODE BEGIN 0 */

static volatile uint32_t canStatusLastRxTick[2];
static volatile uint8_t canStatusRxSeen[2];
static volatile uint8_t canStatusRxError[2];

#define CAN_STATUS_RX_TIMEOUT_MS 2500U
#define CAN_STATUS_RX_PULSE_MS 80U

static bool canStatusBusIsHealthy(CanStatusBus_t bus, uint32_t now)
{
  return (canStatusRxSeen[bus] != 0U) &&
      (canStatusRxError[bus] == 0U) &&
      ((now - canStatusLastRxTick[bus]) < CAN_STATUS_RX_TIMEOUT_MS);
}

static void setCanStatusLeds(GPIO_TypeDef *okPort, uint16_t okPin,
                             GPIO_TypeDef *faultPort, uint16_t faultPin,
                             CanStatusBus_t bus, uint32_t now)
{
  bool healthy = canStatusBusIsHealthy(bus, now);
  bool activity = healthy &&
      ((now - canStatusLastRxTick[bus]) < CAN_STATUS_RX_PULSE_MS);

  HAL_GPIO_WritePin(okPort, okPin, activity ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(faultPort, faultPin, healthy ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CAN_FAULT_GPIO_Port, CAN_FAULT_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CAN_OK_GPIO_Port, CAN_OK_Pin, GPIO_PIN_RESET);

#if defined(VEGA_CAN_OK_GPIO_Port) && defined(VEGA_CAN_OK_Pin) && \
    defined(VEGA_CAN_FAULT_GPIO_Port) && defined(VEGA_CAN_FAULT_Pin)
  HAL_GPIO_WritePin(VEGA_CAN_OK_GPIO_Port, VEGA_CAN_OK_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(VEGA_CAN_FAULT_GPIO_Port, VEGA_CAN_FAULT_Pin, GPIO_PIN_RESET);
#endif

  /*Configure GPIO pin : PtPin */
  GPIO_InitStruct.Pin = CAN_FAULT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CAN_FAULT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PtPin */
  GPIO_InitStruct.Pin = CAN_OK_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CAN_OK_GPIO_Port, &GPIO_InitStruct);

#if defined(VEGA_CAN_OK_GPIO_Port) && defined(VEGA_CAN_OK_Pin) && \
    defined(VEGA_CAN_FAULT_GPIO_Port) && defined(VEGA_CAN_FAULT_Pin)
  GPIO_InitStruct.Pin = VEGA_CAN_OK_Pin;
  HAL_GPIO_Init(VEGA_CAN_OK_GPIO_Port, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = VEGA_CAN_FAULT_Pin;
  HAL_GPIO_Init(VEGA_CAN_FAULT_GPIO_Port, &GPIO_InitStruct);
#endif

}

/* USER CODE BEGIN 2 */
void CAN_StatusLed_RecordRx(CanStatusBus_t bus)
{
  if ((bus == CAN_STATUS_BUS_CANOPEN) || (bus == CAN_STATUS_BUS_VEGA)) {
    canStatusLastRxTick[bus] = HAL_GetTick();
    canStatusRxSeen[bus] = 1U;
    canStatusRxError[bus] = 0U;
  }
}

void CAN_StatusLed_RecordError(CanStatusBus_t bus)
{
  if ((bus == CAN_STATUS_BUS_CANOPEN) || (bus == CAN_STATUS_BUS_VEGA)) {
    canStatusRxError[bus] = 1U;
  }
}

void CAN_StatusLed_Update(void)
{
  uint32_t now = HAL_GetTick();

  setCanStatusLeds(CAN_OK_GPIO_Port, CAN_OK_Pin,
                   CAN_FAULT_GPIO_Port, CAN_FAULT_Pin,
                   CAN_STATUS_BUS_CANOPEN, now);

#if defined(VEGA_CAN_OK_GPIO_Port) && defined(VEGA_CAN_OK_Pin) && \
    defined(VEGA_CAN_FAULT_GPIO_Port) && defined(VEGA_CAN_FAULT_Pin)
  setCanStatusLeds(VEGA_CAN_OK_GPIO_Port, VEGA_CAN_OK_Pin,
                   VEGA_CAN_FAULT_GPIO_Port, VEGA_CAN_FAULT_Pin,
                   CAN_STATUS_BUS_VEGA, now);
#endif
}

/* USER CODE END 2 */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
