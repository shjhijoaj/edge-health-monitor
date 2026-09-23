# Test Report

## 一键验证

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\verify_all.ps1
```

最近一次完整运行的结果（写入 `docs/evidence/verify-summary.json`）：

| 步骤 | 结果 | 说明 |
| --- | --- | --- |
| Python 工具语法检查 | PASS | 4 个文件 |
| 主机测试（MSVC + CTest） | PASS | 100% tests passed, 0 failed out of 2 |
| 主机测试（GCC） | PASS | protocol tests passed / gateway tests passed |
| Cortex-M3 固件交叉编译 | PASS | 生成两个 ELF 镜像 |
| QEMU 看门狗演示 | PASS | 244 帧，错帧 0，看门狗复位 3 次 |

同一份 C/C++ 代码在 MSVC 和 GCC 两套编译器下都通过测试，说明代码没有依赖某个编译器的扩展行为。

## Automated checks

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The protocol test checks:

- sample encode/decode round trip;
- sequence and fixed-point fields surviving a byte-by-byte feed;
- CRC corruption being rejected;
- integer clamp and RMS helpers.

The gateway test checks:

- UART-style byte ingestion;
- CSV header and sample persistence;
- all four alert paths for a deliberately abnormal sample;
- JSON payload generation.

## 本地实测结果（Windows / MSVC 19.41 / NMake）

编译和测试：

```text
100% tests passed, 0 tests failed out of 2
Total Test time (real) = 0.12 sec
```

端到端链路：

| 检查项 | 实测结果 |
| --- | --- |
| C 模拟器持续采集（200 ms 间隔，运行 3 秒） | 生成 15 条采样 |
| 面板实时增长（间隔 3 秒两次采样） | 16 → 19 条，在线，数据延迟 0.8 s |
| 协议原始帧录制 | 24 帧 / 768 字节，帧长 32 字节 |
| 串口桥接回放同一段原始帧 | 24 帧全部解码成功，CRC 错误 0 |
| 独立实现交叉验证 | Python 解码器与 C 编码器结果一致 |
| HTTP 写入一条越限采样 | 返回 201，面板告警数从 9 增加到 10 |
| HTTP 写入非法字段 | 返回 400，未写入数据 |
| 停止数据源后的离线判定 | 7.2 s 后 `online=false` |
| 本地面板页面 | HTTP 200，包含设备状态、趋势、告警和采样表 |

## 仿真目标实测结果（QEMU Cortex-M3 + FreeRTOS）

这部分运行的是真实固件：ARM 交叉编译、FreeRTOS 调度、在模拟的 Cortex-M3 上执行。

| 组件 | 版本 |
| --- | --- |
| 交叉编译器 | arm-none-eabi-gcc 16.1.0 |
| 模拟器 | QEMU 11.1.1 |
| 内核 | FreeRTOS-Kernel（ARM_CM3 端口，heap_4） |
| 目标板 | QEMU `lm3s6965evb`，Cortex-M3，256 KB Flash / 64 KB SRAM |

固件镜像体积：

| 镜像 | text | data | bss | 说明 |
| --- | ---: | ---: | ---: | --- |
| `edge_health_firmware.elf` | 6752 | 8 | 24896 | 含故意故障注入 |
| `edge_health_firmware_stable.elf` | 6652 | 8 | 24896 | 用于稳定性测试 |

看门狗演示（`-Image watchdog -Seconds 90`）：

| 检查项 | 实测结果 |
| --- | --- |
| 墙钟运行时间 | 90 s |
| 固件输出帧 | 191 |
| 主机独立解码还原 | 191 帧，错帧 0 |
| 启动次数 | 3（含 2 次看门狗复位） |
| 看门狗复位次数 | 2 |
| 故障注入到复位恢复 | 完整走通：注入卡死 → 1.5 s 心跳超时 → AIRCR 复位 → `.noinit` 计数保留 |

证据文件：`docs/evidence/watchdog-uart.log`、`watchdog-frames.ehraw`、`metrics-watchdog.json`、`watchdog-telemetry.svg`。

帧数和复位次数会随仿真速度略有波动（QEMU 在同一台机器上每次运行速度不同），例如另一次 90 秒运行的记录是 150 帧、2 次启动、1 次复位。判断标准是错帧必须为 0、故障注入后必须出现复位恢复。Windows 与 Linux 两套运行脚本（`run_emulation.ps1` / `run_emulation.sh`）都验证过同一条链路。

稳定性测试（`-Image stable -Seconds 600`，无故障注入）：

| 检查项 | 实测结果 |
| --- | --- |
| 墙钟运行时间 | 600.3 s（10 分钟） |
| 模拟运行时间 | 147 s |
| 固件输出帧 | 1479 |
| 主机独立解码还原 | 1479 帧，错帧 0 |
| 回放到本地面板 | 1479 帧 |
| 启动次数 | 1 |
| 看门狗复位次数 | 0 |

证据文件：`docs/evidence/stable-uart.log`、`stable-frames.ehraw`、`metrics-stable.json`。

实时流式接入（QEMU 串口经 TCP 直连面板，不经落盘）：

| 检查项 | 实测结果 |
| --- | --- |
| 连接方式 | `-serial tcp:127.0.0.1:5555,server=on` + `serial_bridge.py --tcp` |
| 流格式识别 | 自动识别为十六进制文本流（固件用 `FRAME A5 5A ...` 打印） |
| 单次运行 | 25 s 内解码 15 帧，错帧 0 |
| 断线重连 | 手动关闭 QEMU 再重启，桥接自动重连 2 次，期间重试 3 次 |
| 重连前后 | 各收到帧，序号从 #7 持续到 #20，错帧 0 |
| 面板写入 | 采样数从 2966 增至 3010 |

说明：仿真环境里 QEMU 执行速度低于真实处理器，所以"模拟运行时间"明显小于墙钟时间。两个口径都记录在上表里，引用时不要混用。

## 真实硬件验收清单

下面几项需要在 STM32 板子上实测，代码路径已经准备好，但结果需要真实器件填写：

| 检查项 | 目标 | 结果 |
| --- | --- | --- |
| 115200 波特率下的帧完整性 | 1000 帧，CRC 错误 0 | 待硬件实测 |
| 传感器采样周期 | 100 ms ± 2 ms | 待硬件实测 |
| 拔插串口线后的恢复 | 3 秒内恢复采集 | 待硬件实测 |
| 连续运行 | 30 分钟无看门狗复位 | 待硬件实测 |
