cmake_minimum_required(VERSION 3.16)
project(edge_health_firmware LANGUAGES C ASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(FREERTOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/FreeRTOS-Kernel"
    CACHE PATH "FreeRTOS-Kernel directory (vendored under firmware/third_party)")

if(NOT EXISTS "${FREERTOS_ROOT}/tasks.c")
    message(FATAL_ERROR
        "FreeRTOS kernel not found at ${FREERTOS_ROOT}.\n"
        "The kernel is vendored under firmware/third_party/FreeRTOS-Kernel;\n"
        "see firmware/third_party/FreeRTOS-Kernel/VENDORED.md to restore or update it.")
endif()

set(FREERTOS_PORT "${FREERTOS_ROOT}/portable/GCC/ARM_CM3")

set(FIRMWARE_SOURCES
    app_main.c
    board.c
    startup.c
    ../../src/eh_protocol.c
    ../../src/eh_measurement.c
    ${FREERTOS_ROOT}/tasks.c
    ${FREERTOS_ROOT}/queue.c
    ${FREERTOS_ROOT}/list.c
    ${FREERTOS_ROOT}/portable/MemMang/heap_4.c
    ${FREERTOS_PORT}/port.c
)

set(FIRMWARE_INCLUDES
    .
    ../../include
    ${FREERTOS_ROOT}/include
    ${FREERTOS_PORT}
)

set(FIRMWARE_FLAGS
    -mcpu=cortex-m3
    -mthumb
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)

set(FIRMWARE_LINK_FLAGS
    -mcpu=cortex-m3
    -mthumb
    -nostartfiles
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    -Wl,--gc-sections
    -Wl,-Map=edge_health_firmware.map
)

add_executable(edge_health_firmware ${FIRMWARE_SOURCES})
target_include_directories(edge_health_firmware PRIVATE ${FIRMWARE_INCLUDES})
target_compile_options(edge_health_firmware PRIVATE ${FIRMWARE_FLAGS})
target_link_options(edge_health_firmware PRIVATE ${FIRMWARE_LINK_FLAGS})

set_target_properties(edge_health_firmware PROPERTIES SUFFIX ".elf")

# Second image for the stability run: same firmware without the deliberate
# sampling stall, so a long run can be measured without watchdog resets.
add_executable(edge_health_firmware_stable ${FIRMWARE_SOURCES})
target_include_directories(edge_health_firmware_stable PRIVATE ${FIRMWARE_INCLUDES})
target_compile_options(edge_health_firmware_stable PRIVATE ${FIRMWARE_FLAGS} -DEHM_WATCHDOG_DEMO=0)
target_link_options(edge_health_firmware_stable PRIVATE ${FIRMWARE_LINK_FLAGS})
set_target_properties(edge_health_firmware_stable PROPERTIES
    SUFFIX ".elf"
    OUTPUT_NAME "edge_health_firmware_stable")

foreach(image edge_health_firmware edge_health_firmware_stable)
    add_custom_command(TARGET ${image} POST_BUILD
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${image}>
        COMMENT "Firmware size for ${image}")
endforeach()
