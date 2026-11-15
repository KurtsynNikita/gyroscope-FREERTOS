#ifndef FREERTOS_H
#define FREERTOS_H

#include "cmsis_os.h"

// Публичные макросы 
#define UART_BUFFER_SIZE    32
#define BUTTON_PRESS_FLAG   0x01

// Хэндлы задач и очередей 
extern osThreadId_t ButtonTaskHandle;
extern osMessageQueueId_t UartRxQueueHandle;
extern osMessageQueueId_t GyroQueueHandle;
extern osMutexId_t SharedDataMutexHandle;

void MX_FREERTOS_Init(void);

#endif