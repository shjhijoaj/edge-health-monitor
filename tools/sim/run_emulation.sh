#!/usr/bin/env bash
# Runs the firmware in QEMU, converts the UART capture into protocol frames and
# writes a metrics file. Linux/macOS counterpart of run_emulation.ps1.
#
#   ./tools/sim/run_emulation.sh watchdog 120
set -euo pipefail

image="${1:-watchdog}"
seconds="${2:-120}"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
evidence_dir="$repo_root/docs/evidence"
stage="$(mktemp -d)"
mkdir -p "$evidence_dir"

qemu="${QEMU_SYSTEM_ARM:-qemu-system-arm}"
command -v "$qemu" >/dev/null 2>&1 || {
    echo "找不到 $qemu。Debian/Ubuntu: sudo apt-get install -y qemu-system-arm" >&2
    exit 1
}

if [ "$image" = "stable" ]; then
    elf="$repo_root/build-firmware/edge_health_firmware_stable.elf"
else
    elf="$repo_root/build-firmware/edge_health_firmware.elf"
fi
[ -f "$elf" ] || { echo "找不到固件：$elf（先运行 tools/sim/build_firmware.sh）" >&2; exit 1; }

uart_log="$stage/uart-$image.log"
cp "$elf" "$stage/$image.elf"

# QEMU is a native binary: on MSYS/Cygwin it needs Windows-style paths, while
# on Linux the plain path is already correct.
qemu_uart_path="$uart_log"
qemu_kernel_path="$stage/$image.elf"
if command -v cygpath >/dev/null 2>&1; then
    qemu_uart_path="$(cygpath -w "$uart_log")"
    qemu_kernel_path="$(cygpath -w "$stage/$image.elf")"
fi

echo "在 lm3s6965evb 上运行 $image 固件，$seconds 秒..."
set +e
timeout "$seconds" "$qemu" -M lm3s6965evb -cpu cortex-m3 -display none \
    -serial "file:$qemu_uart_path" -kernel "$qemu_kernel_path" \
    >"$stage/qemu.out" 2>"$stage/qemu.err"
qemu_status=$?
set -e

if [ ! -s "$uart_log" ]; then
    echo "QEMU 没有产生串口输出（退出码 $qemu_status）" >&2
    sed -n '1,5p' "$stage/qemu.err" >&2 || true
    exit 1
fi

frames=$(grep -c '^FRAME ' "$uart_log" || true)
recovered_raw=$(python3 "$repo_root/tools/hexlog_to_raw.py" \
    --input "$uart_log" --output "$evidence_dir/$image-frames.ehraw")
recovered=$(printf '%s\n' "$recovered_raw" | sed -n 's/^frames recovered *: *\([0-9]*\).*/\1/p')
bad=$(printf '%s\n' "$recovered_raw" | sed -n 's/^bad frames *: *\([0-9]*\).*/\1/p')
guest_uptime=$(grep -o 'uptime_ms=[0-9]*' "$uart_log" | tail -n 1 | cut -d= -f2 || true)
boot=$(grep -o 'boot_count=[0-9]*' "$uart_log" | tail -n 1 | cut -d= -f2 || true)
resets=$(grep -o 'wd_resets=[0-9]*' "$uart_log" | tail -n 1 | cut -d= -f2 || true)

cp "$uart_log" "$evidence_dir/$image-uart.log"

cat > "$evidence_dir/metrics-$image.json" <<JSON
{
  "image": "${image}",
  "machine": "lm3s6965evb",
  "wall_seconds": ${seconds},
  "guest_uptime_ms_max": ${guest_uptime:-0},
  "frame_lines": ${frames},
  "frames_recovered": ${recovered:-0},
  "bad_frames": ${bad:-0},
  "frames_replayed": 0,
  "boot_count": ${boot:-0},
  "watchdog_resets": ${resets:-0},
  "generated_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
JSON

printf '%s\n' "$recovered_raw"
echo "输出帧        : $frames"
echo "启动次数      : ${boot:-0}"
echo "看门狗复位    : ${resets:-0}"
echo "证据目录      : $evidence_dir"
