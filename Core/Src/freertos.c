#include "freertos.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include "stm32f4xx_hal.h" 

#include "bsp_gpio.h"
#include "bsp_gyro.h"
#include "bsp_uart.h"

#define MODE_1                  1
#define MODE_2                  2

#define MIN_PERIOD_MS           10
#define MAX_PERIOD_MS           10000
#define DEFAULT_PERIOD_MS       500

#define GYRO_TASK_DELAY_MS      10
#define LED_TASK_DELAY_MS       50
#define BUTTON_DEBOUNCE_MS      50
#define RX_TASK_DELAY_MS        1000
#define STARTUP_DELAY_MS        100

#define GYRO_QUEUE_LENGTH       10
#define UART_QUEUE_LENGTH       5
#define TX_BUFFER_SIZE          64

#define STACK_SIZE_NORMAL       (256 * 4)
#define STACK_SIZE_SMALL        (128 * 4)

typedef struct { 
    int16_t x; 
    int16_t y; 
    int16_t z; 
} GyroData_t;

static uint8_t current_mode = MODE_1;
static uint32_t period_ms = DEFAULT_PERIOD_MS;

// Конфигурация задач
osThreadId_t GyroTaskHandle; // Объявление хэндла
const osThreadAttr_t GyroTask_attributes = {
  .name = "GyroTask",
  .stack_size = STACK_SIZE_NORMAL,
  .priority = (osPriority_t) osPriorityNormal,
}; // Создание структуры с атрибутами

osThreadId_t UartTxTaskHandle;
const osThreadAttr_t UartTxTask_attributes = {
  .name = "UartTxTask",
  .stack_size = STACK_SIZE_NORMAL,
  .priority = (osPriority_t) osPriorityNormal,
};

osThreadId_t UartRxTaskHandle;
const osThreadAttr_t UartRxTask_attributes = {
  .name = "UartRxTask",
  .stack_size = STACK_SIZE_NORMAL,
  .priority = (osPriority_t) osPriorityNormal,
};

osThreadId_t ButtonTaskHandle;
const osThreadAttr_t ButtonTask_attributes = {
  .name = "ButtonTask",
  .stack_size = STACK_SIZE_SMALL,
  .priority = (osPriority_t) osPriorityHigh,
};

osThreadId_t ValidationTaskHandle;
const osThreadAttr_t ValidationTask_attributes = {
  .name = "ValidationTask",
  .stack_size = STACK_SIZE_NORMAL,
  .priority = (osPriority_t) osPriorityNormal,
};

osThreadId_t LedTaskHandle;
const osThreadAttr_t LedTask_attributes = {
  .name = "LedTask",
  .stack_size = STACK_SIZE_SMALL,
  .priority = (osPriority_t) osPriorityLow,
};

// Конфигурация очередей
osMessageQueueId_t GyroQueueHandle;
const osMessageQueueAttr_t GyroQueue_attributes = {
  .name = "GyroQueue"
};

osMessageQueueId_t UartRxQueueHandle;
const osMessageQueueAttr_t UartRxQueue_attributes = {
  .name = "UartRxQueue"
};

// Конфигурация мьютекса
osMutexId_t SharedDataMutexHandle;
const osMutexAttr_t SharedDataMutex_attributes = {
  .name = "SharedDataMutex"
};

// Прототипы
void GyroTask_Entry(void *argument);
void UartTxTask_Entry(void *argument);
void UartRxTask_Entry(void *argument);
void ButtonTask_Entry(void *argument);
void ValidationTask_Entry(void *argument);
void LedTask_Entry(void *argument);
extern void GpioWrite(GPIO_TypeDef *port, uint8_t pin, uint8_t value);
extern uint8_t GpioRead(GPIO_TypeDef *port, uint8_t pin);
extern UART_HandleTypeDef huart1;
extern SPI_HandleTypeDef hspi5;
void Gyro_Init(void);
void Gyro_ReadXYZ(int16_t *x, int16_t *y, int16_t *z);

void MX_FREERTOS_Init(void) {

  // Создание мьютекса
  SharedDataMutexHandle = osMutexNew(&SharedDataMutex_attributes);

  // Создание очередей
  GyroQueueHandle = osMessageQueueNew (10, 6, &GyroQueue_attributes);
  UartRxQueueHandle = osMessageQueueNew (5, 32, &UartRxQueue_attributes);

  // Создание задач
  GyroTaskHandle = osThreadNew(GyroTask_Entry, NULL, &GyroTask_attributes);
  UartTxTaskHandle = osThreadNew(UartTxTask_Entry, NULL, &UartTxTask_attributes);
  UartRxTaskHandle = osThreadNew(UartRxTask_Entry, NULL, &UartRxTask_attributes);
  ButtonTaskHandle = osThreadNew(ButtonTask_Entry, NULL, &ButtonTask_attributes);
  ValidationTaskHandle = osThreadNew(ValidationTask_Entry, NULL, &ValidationTask_attributes);
  LedTaskHandle = osThreadNew(LedTask_Entry, NULL, &LedTask_attributes);
}

// Реализация задач
void GyroTask_Entry(void *argument) {
  GyroData_t gyro_data;

  Gyro_Calibrate(); 

  osDelay(100); // Задержка, чтобы другие задачи успели запуститься

  for(;;) {
    Gyro_ReadXYZ(&gyro_data.x, &gyro_data.y, &gyro_data.z);
    osMessageQueuePut(GyroQueueHandle, &gyro_data, 0, 0); // Отправка данных в очередь
    osDelay(10); // Усыпляем задачу на 10 мс, чтобы пришли новые данные гироскопа
  }
}

void UartTxTask_Entry(void *argument) {
  GyroData_t rx_data; // Данные из очереди
  char tx_buffer[64];
  uint32_t last_print = 0;
  for(;;) {
    if (osMessageQueueGet(GyroQueueHandle, &rx_data, NULL, osWaitForever) == osOK) { // Спит пока в очереди пусто
      osMutexAcquire(SharedDataMutexHandle, osWaitForever); // Поднимаем мьютекc
      uint8_t mode = current_mode; // Сохраняем значения в локальные переменные
      uint32_t period = period_ms;
      osMutexRelease(SharedDataMutexHandle); // Опускаем мьютекс
      
      if (mode == 2 && (osKernelGetTickCount() - last_print >= period)) { // выводим данные только во втором режиме
        last_print = osKernelGetTickCount();
        int len = snprintf(tx_buffer, sizeof(tx_buffer), "X:%d Y:%d Z:%d\r\n", rx_data.x, rx_data.y, rx_data.z);
        HAL_UART_Transmit(&huart1, (uint8_t*)tx_buffer, len, 100);
      }
    }
  }
}

void UartRxTask_Entry(void *argument) {
  // Запускаем асинхронный прием ОДИН раз при старте задачи
  BSP_UART_StartReceive(); 
  
  for(;;) {
    osDelay(RX_TASK_DELAY_MS);
  }
}

void ButtonTask_Entry(void *argument) {
  for(;;) {
    osThreadFlagsWait(0x01, osFlagsWaitAny, osWaitForever); // Ждем прерывание от кнопки с флагом 0x001 и сбрасываем его
    osDelay(50); // Антидребезг
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
      osMutexAcquire(SharedDataMutexHandle, osWaitForever);
      current_mode = (current_mode == 1) ? 2 : 1; // Смена режима
      osMutexRelease(SharedDataMutexHandle);
    }
  }
}

void ValidationTask_Entry(void *argument) {
  char rx_str[32];
  char tx_buffer[64];
  for(;;) {
    if (osMessageQueueGet(UartRxQueueHandle, rx_str, NULL, osWaitForever) == osOK) { // Ждем пока не соберется целая строка
      osMutexAcquire(SharedDataMutexHandle, osWaitForever);
      uint8_t mode = current_mode; // Безопасно читаем режим
      osMutexRelease(SharedDataMutexHandle);
      
      if (mode == 1) {
        int value = atoi(rx_str);
        if (value >= 10 && value <= 10000) {
          osMutexAcquire(SharedDataMutexHandle, osWaitForever);
          period_ms = value; // Безопасно меняем значение периода 
          osMutexRelease(SharedDataMutexHandle);
          int len = snprintf(tx_buffer, sizeof(tx_buffer), "OK: %d ms\r\n", value);
          HAL_UART_Transmit(&huart1, (uint8_t*)tx_buffer, len, 100);
        } else {
          int len = snprintf(tx_buffer, sizeof(tx_buffer), "Error: 10..10000\r\n");
          HAL_UART_Transmit(&huart1, (uint8_t*)tx_buffer, len, 100);
        }
      }
    }
  }
}

void LedTask_Entry(void *argument) {
  uint8_t last_mode = 0;
  for(;;) {
    osMutexAcquire(SharedDataMutexHandle, osWaitForever);
    uint8_t mode = current_mode;
    osMutexRelease(SharedDataMutexHandle);
    
    if (mode != last_mode) {
      if (mode == 1) {
        GpioWrite(GPIOG, 14, 1); // Red on
        GpioWrite(GPIOG, 13, 0); // Green off
      } else {
        GpioWrite(GPIOG, 14, 0);
        GpioWrite(GPIOG, 13, 1);
      }
      last_mode = mode;
    }
    osDelay(50);
  }
}





