cmake_minimum_required(VERSION 3.20)

# Имя проекта и языки
project(GyroRTOS_CMake C ASM)

# Устанавливаем стандарты языка
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# === 1. Поиск всех исходных файлов ===
file(GLOB_RECURSE SOURCES 
    "Core/Src/*.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/*.c"
    "Middlewares/Third_Party/FreeRTOS/Source/*.c"
)

# Исключаем файлы-шаблоны HAL
list(FILTER SOURCES EXCLUDE REGEX ".*_template\\.c$")

# Исключаем лишние менеджеры памяти FreeRTOS (оставляем только heap_4)
list(FILTER SOURCES EXCLUDE REGEX ".*heap_1\\.c$")
list(FILTER SOURCES EXCLUDE REGEX ".*heap_2\\.c$")
list(FILTER SOURCES EXCLUDE REGEX ".*heap_3\\.c$")
list(FILTER SOURCES EXCLUDE REGEX ".*heap_5\\.c$")

# Исключаем порты для других компиляторов (RVDS, IAR) и лишние менеджеры памяти
list(FILTER SOURCES EXCLUDE REGEX ".*portable/RVDS.*")
list(FILTER SOURCES EXCLUDE REGEX ".*portable/IAR.*")
list(FILTER SOURCES EXCLUDE REGEX ".*portable/MemMang.*")

# Вручную добавляем только нужный порт GCC и heap_4
list(APPEND SOURCES
    "Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c"
    "Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c"
    "startup_stm32f429xx.s")

# === 2. Пути к заголовочным файлам (Include Paths) ===
include_directories(
    "Core/Inc"
    "Drivers/STM32F4xx_HAL_Driver/Inc"
    "Drivers/STM32F4xx_HAL_Driver/Inc/Legacy"
    "Drivers/CMSIS/Device/ST/STM32F4xx/Include"
    "Drivers/CMSIS/Include"
    "Middlewares/Third_Party/FreeRTOS/Source/include"
    "Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2"
    "Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F"
)

# === 3. Макросы компиляции (Define) ===
add_compile_definitions(
    USE_HAL_DRIVER
    STM32F429xx
)

# === 4. Флаги компилятора ===
add_compile_options(
    -Og -Wall -fdata-sections -ffunction-sections -g -gdwarf-2
)

# === 5. Создание исполняемого файла (линковка) ===
add_executable(${PROJECT_NAME}.elf ${SOURCES})

# Настройки линковщика
target_link_options(${PROJECT_NAME}.elf PRIVATE
    -T${CMAKE_SOURCE_DIR}/STM32F429xx_FLASH.ld
    -specs=nano.specs
    -lc -lm 
    # Убрали -lnosys, так как у нас есть свой syscalls.c и sysmem.c!
    -Wl,-Map=${PROJECT_NAME}.map,--cref
    -Wl,--gc-sections
)

# === 6. Генерация .bin и .hex файлов (как в Makefile) ===
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary -S $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
    COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
    COMMENT "Generating BIN and HEX files"
)