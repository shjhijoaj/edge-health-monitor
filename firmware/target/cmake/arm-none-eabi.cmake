# Cross-compilation toolchain for the emulated Cortex-M3 target.
#
# Usage:
#   cmake -S firmware/target -B build-firmware \
#         -DCMAKE_TOOLCHAIN_FILE=firmware/target/cmake/arm-none-eabi.cmake \
#         -DCMAKE_BUILD_TYPE=Release

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(ARM_TOOLCHAIN_ROOT "C:/msys64/ucrt64/bin" CACHE PATH "Directory holding arm-none-eabi tools")

set(CMAKE_C_COMPILER "${ARM_TOOLCHAIN_ROOT}/arm-none-eabi-gcc.exe")
set(CMAKE_ASM_COMPILER "${ARM_TOOLCHAIN_ROOT}/arm-none-eabi-gcc.exe")
set(CMAKE_OBJCOPY "${ARM_TOOLCHAIN_ROOT}/arm-none-eabi-objcopy.exe")
set(CMAKE_SIZE "${ARM_TOOLCHAIN_ROOT}/arm-none-eabi-size.exe")

# The board has no operating system; do not try to link a hosted executable.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
