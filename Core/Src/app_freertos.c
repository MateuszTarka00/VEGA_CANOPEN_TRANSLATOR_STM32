/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
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
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tim.h"
#include "fdcan.h"
#include "softwareTimer_ms.h"
#include "iwdg.h"
#include "canOpenManager.h"
#include "canOpenLopDefinitions.h"
#include "vegaCanManager.h"
#include "protocolUtils.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CANOPEN_TASK_DELAY_MS 			1
#define HEARTBEAT_INTERVAL_MS			1000
#define VEGA_TX_INTERVAL_MS				100
#define CANOPEN_ID						1
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for TrancieverT */
osThreadId_t TrancieverTHandle;
const osThreadAttr_t TrancieverT_attributes = {
  .name = "TrancieverT",
  .priority = (osPriority_t) osPriorityNormal3,
  .stack_size = 512 * 4
};
/* Definitions for CanOpenMenagerT */
osThreadId_t CanOpenMenagerTHandle;
const osThreadAttr_t CanOpenMenagerT_attributes = {
  .name = "CanOpenMenagerT",
  .priority = (osPriority_t) osPriorityNormal4,
  .stack_size = 1024 * 4
};
/* Definitions for VegaRxT */
osThreadId_t VegaRxTHandle;
const osThreadAttr_t VegaRxT_attributes = {
  .name = "VegaRxT",
  .priority = (osPriority_t) osPriorityNormal2,
  .stack_size = 512 * 4
};
/* Definitions for CanOpenRxT */
osThreadId_t CanOpenRxTHandle;
const osThreadAttr_t CanOpenRxT_attributes = {
  .name = "CanOpenRxT",
  .priority = (osPriority_t) osPriorityNormal1,
  .stack_size = 512 * 4
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void tranciever(void *argument);
void canOpenMenager(void *argument);
void vegaRx(void *argument);
void canOpenRx(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationIdleHook(void);
void vApplicationTickHook(void);
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);
void vApplicationDaemonTaskStartupHook(void);

/* USER CODE BEGIN 2 */
void vApplicationIdleHook( void )
{
   /* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
   to 1 in FreeRTOSConfig.h. It will be called on each iteration of the idle
   task. It is essential that code added to this hook function never attempts
   to block in any way (for example, call xQueueReceive() with a block time
   specified, or call vTaskDelay()). If the application makes use of the
   vTaskDelete() API function (as this demo application does) then it is also
   important that vApplicationIdleHook() is permitted to return to its calling
   function, because it is the responsibility of the idle task to clean up
   memory allocated by the kernel to any task that has since been deleted. */
}
/* USER CODE END 2 */

/* USER CODE BEGIN 3 */
void vApplicationTickHook( void )
{
   /* This function will be called by each tick interrupt if
   configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h. User code can be
   added here, but the tick hook is called from an interrupt context, so
   code must not attempt to block, and only the interrupt safe FreeRTOS API
   functions can be used (those that end in FromISR()). */
}
/* USER CODE END 3 */

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
   /* vApplicationMallocFailedHook() will only be called if
   configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
   function that will get called if a call to pvPortMalloc() fails.
   pvPortMalloc() is called internally by the kernel whenever a task, queue,
   timer or semaphore is created. It is also called by various parts of the
   demo application. If heap_1.c or heap_2.c are used, then the size of the
   heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
   FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
   to query the size of free heap space that remains (although it does not
   provide information on how the remaining heap might be fragmented). */
}
/* USER CODE END 5 */

/* USER CODE BEGIN DAEMON_TASK_STARTUP_HOOK */
void vApplicationDaemonTaskStartupHook(void)
{
}
/* USER CODE END DAEMON_TASK_STARTUP_HOOK */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
	CANOPEN_InitRTOS();
	VEGA_InitRTOS();
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of TrancieverT */
  TrancieverTHandle = osThreadNew(tranciever, NULL, &TrancieverT_attributes);

  /* creation of CanOpenMenagerT */
  CanOpenMenagerTHandle = osThreadNew(canOpenMenager, NULL, &CanOpenMenagerT_attributes);

  /* creation of VegaRxT */
  VegaRxTHandle = osThreadNew(vegaRx, NULL, &VegaRxT_attributes);

  /* creation of CanOpenRxT */
  CanOpenRxTHandle = osThreadNew(canOpenRx, NULL, &CanOpenRxT_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_tranciever */
/**
  * @brief  Function implementing the TrancieverT thread.
  * @brief  Responsible for:
  *         - Transmitting VEGA protocol messages to elevator panel
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_tranciever */
void tranciever(void *argument)
{
  /* USER CODE BEGIN tranciever */
  /* Infinite loop */
  for(;;)
  {
    /* Transmit VEGA messages to elevator panel based on node states */
    vegaTransmitSubTask();
    
    osDelay(10);
  }
  /* USER CODE END tranciever */
}

/* USER CODE BEGIN Header_canOpenMenager */
/**
* @brief Function implementing the CanOpenMenagerT thread.
* @brief Responsible for:
*        - Sending CANopen master heartbeat every 1 second
*        - Processing received CANopen messages (LOP messages, button states, device config)
*        - Managing CANopen node state machine and NMT transitions
*        - Handling SDO requests for device configuration
*        - Transmitting CANopen node messages (LED states, floor display)
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_canOpenMenager */
void canOpenMenager(void *argument)
{
  /* USER CODE BEGIN canOpenMenager */
  CanOpenNodeObject* nodePtr = NULL;
  TickType_t lastHeartbeatTime = xTaskGetTickCount();
  
	  /* Infinite loop */
	  for(;;)
	  {
	    HAL_IWDG_Refresh(&hiwdg);
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CANOPEN_TASK_DELAY_MS));
		
		TickType_t currentTime = xTaskGetTickCount();
		
		/* Send master heartbeat every 1 second */
		if ((currentTime - lastHeartbeatTime) >= pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS))
		{
		  CANOPEN_SendMasterHeartbeat();
		  lastHeartbeatTime = currentTime;
		}
		
		/* Process all nodes for NMT state transitions and LOP requests */
		nodePtr = getCanOpenObjectsList();
		while (nodePtr != NULL)
		{
		  /* Handle NMT state transitions: PRE_OPERATIONAL -> OPERATIONAL */
		  if (nodePtr->canOpenNodeHandler.nmtState == CO_NMT_PRE_OPERATIONAL)
		  {
			/* Send NMT start command to transition to operational state */
			uint8_t nmtMessage[2] = {1, nodePtr->canOpenNodeHandler.canOpenID};
			protocolSend(0x000, nmtMessage, 2, PROTOCOL_CANOPEN);
		  }
		  
		  /* Handle LOP requests: Send after NMT handshake initiated */
		  if (nodePtr->canOpenNodeHandler.lopRequestNeeded)
		  {
			/* Check if enough time has passed since last request (500ms retry interval) */
			if ((currentTime - nodePtr->canOpenNodeHandler.lastLopRequestTime) >= pdMS_TO_TICKS(500))
			{
			  /* Send LOP request */
			  LOP_RequestAssignment(&nodePtr->canOpenNodeHandler);
			  
			  /* Update tracking: increment attempts and record time */
			  nodePtr->canOpenNodeHandler.lastLopRequestTime = currentTime;
			  nodePtr->canOpenNodeHandler.lopRequestAttempts++;
			  
			  /* Clear flag after max attempts (3 tries) to avoid infinite retry */
			  if (nodePtr->canOpenNodeHandler.lopRequestAttempts >= 3)
			  {
				nodePtr->canOpenNodeHandler.lopRequestNeeded = FALSE;
			  }
			}
		  }
		  
		  /* Process and send CANopen node messages (LED updates, floor display, etc) */
		  processNodeToSendMsg(&nodePtr->canOpenNodeHandler);
		  
		  nodePtr = nodePtr->nextObject;
		}
	  }
  /* USER CODE END canOpenMenager */
}

/* USER CODE BEGIN Header_vegaRx */
/**
* @brief Function implementing the VegaRxT thread.
* @brief Responsible for:
*        - Processing received VEGA protocol messages from queue
*        - Updating CANopen node LED states based on button/arrow presses
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_vegaRx */
void vegaRx(void *argument)
{
  /* USER CODE BEGIN vegaRx */
  CAN_Message_t msg;
  
  /* Infinite loop */
  for(;;)
  {
    /* Process received VEGA messages from queue */
    if (xQueueReceive(vegaRxQueue, &msg, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      /* Update CANopen node LED states from VEGA button messages */
      processVegaMessage(&msg);
    }
  }
  /* USER CODE END vegaRx */
}

/* USER CODE BEGIN Header_canOpenRx */
/**
* @brief Function implementing the CanOpenRxT thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_canOpenRx */
void canOpenRx(void *argument)
{
  /* USER CODE BEGIN canOpenRx */
  /* Infinite loop */
  for(;;)
  {
	CAN_Message_t msg;
	if(xQueueReceive(canOpenRxQueue, &msg, portMAX_DELAY) == pdTRUE)
	{
		HAL_IWDG_Refresh(&hiwdg);
		processCanOpenMessage(&msg);
	}
  }
  /* USER CODE END canOpenRx */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
