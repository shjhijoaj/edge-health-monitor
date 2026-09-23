#!/usr/bin/env bash
# Builds the Cortex-M3 firmware images with the ARM cross compiler (Linux/macOS).
#
#   ./tools/sim/build_firmware.sh
#
# Produces build-firmware/edge_health_firmware.elf (watchdog demo) and
# build-firmware/edge_health_firmware_stable.elf (no injected fault).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
freertos_root="${FREERTOS_ROOT:-$repo_root/.tools/FreeRTOS-Kernel-main}"
build_dir="$repo_root/build-firmware"
prefix="${ARM_TOOLCHAIN_PREFIX:-arm-none-eabi-}"

gcc="${prefix}gcc"
size="${prefix}size"

command -v "$gcc" >/dev/null 2>&1 || {
    echo "找不到 $gcc。Debian/Ubuntu: sudo apt-get install -y gcc-arm-none-eabi" >&2
    exit 1
}
[ -f "$freertos_root/tasks.c" ] || {
    echo "找不到 FreeRTOS 内核：$freertos_root（见 docs/emulation.md 的下载步骤）" >&2
    exit 1
}

port="$freertos_root/portable/GCC/ARM_CM3"
mkdir -p "$build_dir"

sources=(
    "$repo_root/firmware/target/app_main.c"
    "$repo_root/firmware/target/board.c"
    "$repo_root/firmware/target/startup.c"
    "$repo_root/firmware/src/eh_protocol.c"
    "$repo_root/firmware/src/eh_measurement.c"
    "$repo_root/firmware/target/sensors/i2c_bus_sim.c"
    "$repo_root/firmware/target/sensors/i2c_bus_hal.c"
    "$repo_root/firmware/target/sensors/sensor.c"
    "$repo_root/firmware/target/sensors/sensor_i2c.c"
    "$repo_root/firmware/target/sensors/sensor_sim.c"
    "$freertos_root/tasks.c"
    "$freertos_root/queue.c"
    "$freertos_root/list.c"
    "$freertos_root/portable/MemMang/heap_4.c"
    "$port/port.c"
)

common_flags=(-mcpu=cortex-m3 -mthumb -std=gnu99 -Os -ffunction-sections -fdata-sections
              -fno-common -Wall
              -I"$repo_root/firmware/target"
              -I"$repo_root/firmware/target/sensors"
              -I"$repo_root/firmware/include"
              -I"$freertos_root/include"
              -I"$port")

link_flags=(-mcpu=cortex-m3 -mthumb -nostartfiles
            -T"$repo_root/firmware/target/linker.ld"
            -Wl,--gc-sections)

build_image() {
    local name="$1"; shift
    local extra_flags=("$@")
    local objects=()

    for source in "${sources[@]}"; do
        local object="$build_dir/$name-$(basename "${source%.*}").o"
        echo "  编译 $(basename "$source")"
        "$gcc" "${common_flags[@]}" "${extra_flags[@]}" -c "$source" -o "$object"
        objects+=("$object")
    done

    echo "  链接 $name.elf"
    "$gcc" "${objects[@]}" "${link_flags[@]}" \
        -Wl,-Map="$build_dir/$name.map" -o "$build_dir/$name.elf"
    "$size" "$build_dir/$name.elf"
}

echo '构建看门狗演示固件（含故意的采样卡死注入）'
build_image edge_health_firmware -DEHM_WATCHDOG_DEMO=1

echo '构建稳定性测试固件（无故障注入）'
build_image edge_health_firmware_stable -DEHM_WATCHDOG_DEMO=0

echo
echo "固件镜像目录: $build_dir"
