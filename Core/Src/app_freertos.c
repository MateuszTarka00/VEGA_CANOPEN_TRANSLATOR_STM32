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
#include "fdcan.h"
#include "canOpenManager.h"
#include "canOpenLopDefinitions.h"
#include "vegaCanManager.h"
#include "protocolUtils.h"
#include "canopen_port.h"
#include "canopen_nmt.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CANOPEN_TASK_DELAY_MS 			1
#define BOOT_TRIAL_STABLE_MS             1000U
#define BOOT_TRIAL_STARTUP_TIMEOUT_MS    5000U
#define TASK_STARTED_TRANSCEIVER         0x01U
#define TASK_STARTED_CANOPEN_MANAGER     0x02U
#define TASK_STARTED_VEGA_RX             0x04U
#define TASK_STARTED_CANOPEN_RX          0x08U
#define TASK_STARTED_ALL                 0x0FU
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
  .priority = (osPriority_t) osPriorityAboveNormal7,
  .stack_size = 512 * 4
};
/* Definitions for CanOpenMenagerT */
osThreadId_t CanOpenMenagerTHandle;
const osThreadAttr_t CanOpenMenagerT_attributes = {
  .name = "CanOpenMenagerT",
  .priority = (osPriority_t) osPriorityAboveNormal6,
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
static volatile uint8_t critical_task_started;
static volatile bool update_service_requested;
static volatile uint8_t update_service_ready;

static void updateServiceCheckpoint(uint8_t task_bit)
{
  for (;;) {
    taskENTER_CRITICAL();
    if (!update_service_requested) {
      update_service_ready &= (uint8_t)~task_bit;
      taskEXIT_CRITICAL();
      return;
    }
    update_service_ready |= task_bit;
    taskEXIT_CRITICAL();
    osDelay(1U);
  }
}

bool App_EnterUpdateService(void)
{
  uint32_t started = HAL_GetTick();
  update_service_requested = true;
  while (update_service_ready !=
         (TASK_STARTED_TRANSCEIVER | TASK_STARTED_CANOPEN_MANAGER | TASK_STARTED_VEGA_RX)) {
    if ((HAL_GetTick() - started) >= 1000U) {
      update_service_requested = false;
      return false;
    }
    osDelay(1U);
  }
  return true;
}

void App_LeaveUpdateService(void)
{
  update_service_requested = false;
}

static void markTaskStarted(uint8_t task_bit)
{
  taskENTER_CRITICAL();
  critical_task_started |= task_bit;
  taskEXIT_CRITICAL();
}

static bool taskIsHealthy(osThreadId_t task)
{
  eTaskState state;
  if (task == NULL) {
    return false;
  }
  state = eTaskGetState(task);
  return (state == eRunning) || (state == eReady) || (state == eBlocked);
}

static bool startupSelfTestPassed(void)
{
  return (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) &&
      (critical_task_started == TASK_STARTED_ALL) &&
      taskIsHealthy(TrancieverTHandle) &&
      taskIsHealthy(CanOpenMenagerTHandle) &&
      taskIsHealthy(VegaRxTHandle) &&
      taskIsHealthy(CanOpenRxTHandle) &&
      (HAL_FDCAN_GetState(&hfdcan1) == HAL_FDCAN_STATE_BUSY) &&
      (HAL_FDCAN_GetState(&hfdcan2) == HAL_FDCAN_STATE_BUSY) &&
      CanopenPort_Ready();
}

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
  if ((TrancieverTHandle == NULL) || (CanOpenMenagerTHandle == NULL) ||
      (VegaRxTHandle == NULL) || (CanOpenRxTHandle == NULL)) {
    Error_Handler();
  }

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
  markTaskStarted(TASK_STARTED_TRANSCEIVER);
  /* Infinite loop */
  for(;;)
  {
    updateServiceCheckpoint(TASK_STARTED_TRANSCEIVER);
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
*        - Monitoring remote node heartbeat timeouts
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
  markTaskStarted(TASK_STARTED_CANOPEN_MANAGER);
  
	  /* Infinite loop */
	  for(;;)
	  {
        updateServiceCheckpoint(TASK_STARTED_CANOPEN_MANAGER);
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CANOPEN_TASK_DELAY_MS));
		
		TickType_t currentTime = xTaskGetTickCount();
		
		/* Local boot-up/heartbeat and OD are owned by CanOpenRxT. */

		/* Flag nodes that have not sent a heartbeat within the timeout window */
		CANOPEN_CheckHeartbeatTimeouts();
		
		/* Process all nodes for NMT state transitions and LOP requests */
		nodePtr = getCanOpenObjectsList();
		while (nodePtr != NULL)
		{
		  /* Handle NMT state transitions: PRE_OPERATIONAL -> OPERATIONAL */
		  if ((nodePtr->canOpenNodeHandler.nmtState == CO_NMT_PRE_OPERATIONAL) &&
		      (!nodePtr->canOpenNodeHandler.nmtStartSent ||
		       ((currentTime - nodePtr->canOpenNodeHandler.lastNmtStartTime) >= pdMS_TO_TICKS(500U))))
		  {
			/* Send NMT start command to transition to operational state */
			CoFrame command;
			if (CoNmt_Command(&command, 1U, (uint8_t)nodePtr->canOpenNodeHandler.canOpenID)) {
				if (protocolSend(command.id, command.data, command.length, PROTOCOL_CANOPEN)) {
					nodePtr->canOpenNodeHandler.nmtStartSent = TRUE;
					nodePtr->canOpenNodeHandler.lastNmtStartTime = currentTime;
				}
			}
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
  markTaskStarted(TASK_STARTED_VEGA_RX);
  
  /* Infinite loop */
  for(;;)
  {
    updateServiceCheckpoint(TASK_STARTED_VEGA_RX);
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
* @brief Owns local OD/SDO, boot-up, heartbeat and EMCY services, and routes remote LOP traffic.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_canOpenRx */
void canOpenRx(void *argument)
{
  /* USER CODE BEGIN canOpenRx */
  uint32_t healthy_since = 0U;
  uint32_t trial_started_at;
  bool trial_check_done = false;
  CanopenPort_Init();
  markTaskStarted(TASK_STARTED_CANOPEN_RX);
  trial_started_at = HAL_GetTick();
  /* Infinite loop */
  for(;;)
  {
	CAN_Message_t msg;
    bool self_test_passed;
	CanopenPort_Poll();
    self_test_passed = startupSelfTestPassed();
    if (!trial_check_done && CanopenPort_TrialPending() &&
        !self_test_passed &&
        ((HAL_GetTick() - trial_started_at) >= BOOT_TRIAL_STARTUP_TIMEOUT_MS)) {
      NVIC_SystemReset();
    }
    if (!trial_check_done && self_test_passed) {
      uint32_t now = HAL_GetTick();
      if (healthy_since == 0U) {
        healthy_since = now;
      } else if ((now - healthy_since) >= BOOT_TRIAL_STABLE_MS) {
        if (CanopenPort_TrialPending() && !CanopenPort_ConfirmTrial()) {
          NVIC_SystemReset();
        }
        trial_check_done = true;
      }
    } else if (!self_test_passed) {
      healthy_since = 0U;
    }
	if (!CanopenPort_Ready()) {
		osDelay(1U);
		continue;
	}
	if(xQueueReceive(canOpenRxQueue, &msg, pdMS_TO_TICKS(1U)) == pdTRUE)
	{
		if (!CanopenPort_Receive(&msg)) {
			processCanOpenMessage(&msg);
		}
	}
  }
  /* USER CODE END canOpenRx */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
