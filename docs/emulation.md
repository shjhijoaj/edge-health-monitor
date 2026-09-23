# 本地 Cortex-M 仿真

这份文档说明如何在没有开发板的情况下，让**真实的固件代码**跑在**模拟的 Cortex-M3 处理器**上，并把串口输出接回本地面板。

## 为什么这样做

主机模拟器（`gateway/src/main.cpp`）验证的是协议和网关，但它毕竟跑在 x86 上。仿真这条路补上的是：固件代码经过 ARM 交叉编译、在 Cortex-M3 上执行、由 FreeRTOS 调度多任务、通过串口输出协议帧。真正的传感器读数仍然没有，但"固件能在目标架构上跑起来"这件事变成可验证的了。

## 工具链

| 组件 | 版本 | 用途 |
| --- | --- | --- |
| ARM GNU Toolchain | 16.1.0（MSYS2 ucrt64 包 `arm-none-eabi-gcc`） | 交叉编译固件 |
| QEMU | 11.1.1（MSYS2 ucrt64 包 `qemu`） | 模拟 Cortex-M3 与串口 |
| FreeRTOS-Kernel | main 分支源码 | 实时内核 |

安装命令：

```powershell
C:\msys64\usr\bin\pacman.exe -S --noconfirm --needed `
    mingw-w64-ucrt-x86_64-arm-none-eabi-gcc `
    mingw-w64-ucrt-x86_64-qemu
```

FreeRTOS 内核不在仓库里（`.tools/` 已被忽略），需要先下载一次，大约 4 MB：

```powershell
cd <仓库根目录>
New-Item -ItemType Directory -Force -Path .tools | Out-Null
Invoke-WebRequest `
    -Uri 'https://codeload.github.com/FreeRTOS/FreeRTOS-Kernel/zip/refs/heads/main' `
    -OutFile '.tools\FreeRTOS-Kernel.zip'
Expand-Archive '.tools\FreeRTOS-Kernel.zip' -DestinationPath '.tools' -Force
```

解压后应该是 `.tools\FreeRTOS-Kernel-main\`，里面能看到 `tasks.c`、`queue.c`、`portable\GCC\ARM_CM3\`。如果目录名不同，用 `-FreeRtosRoot` 参数指定，或设置 CMake 的 `FREERTOS_ROOT` 变量。

## 目标平台

QEMU 的 `lm3s6965evb`：Stellaris LM3S6965 评估板，ARM Cortex-M3，256 KB Flash、64 KB SRAM，UART0 是 PL011，映射在 `0x4000C000`。

选它的原因是核：STM32F103 也是 Cortex-M3。固件里除 `board.c` 之外的部分（协议、任务逻辑、看门狗处理）在两者之间可以原样复用，换成 STM32 只需要把 UART 驱动换成 HAL 调用。

## 固件结构

| 文件 | 作用 |
| --- | --- |
| [`firmware/target/board.c`](../firmware/target/board.c) | 串口驱动，同时支持 PL011 与 CMSDK 两种寄存器布局 |
| [`firmware/target/board.h`](../firmware/target/board.h) | 板级接口与编译期板卡选择 |
| [`firmware/target/startup.c`](../firmware/target/startup.c) | 中断向量表、`.data` 拷贝、`.bss` 清零 |
| [`firmware/target/linker.ld`](../firmware/target/linker.ld) | Flash/SRAM 布局，含保留跨复位数据的 `.noinit` |
| [`firmware/target/app_main.c`](../firmware/target/app_main.c) | FreeRTOS 任务、队列、看门狗与故障注入 |
| [`firmware/target/FreeRTOSConfig.h`](../firmware/target/FreeRTOSConfig.h) | 内核配置：1 kHz tick、优先级位宽、堆大小 |

协议层直接复用仓库里的 [`firmware/src/eh_protocol.c`](../firmware/src/eh_protocol.c)，与主机测试用的是同一份代码。

## 任务划分

| 任务 | 优先级 | 周期 | 职责 |
| --- | ---: | --- | --- |
| `watchdog_task` | 4（最高） | 200 ms | 监视采样心跳，超时则强制复位 |
| `tx_task` | 3 | 事件驱动 | 从队列取帧，按十六进制写串口 |
| `sample_task` | 2 | 100 ms | 生成采样、调用 `eh_encode_sample`、入队 |
| `health_task` | 1 | 1000 ms | 输出启动次数、复位次数、帧数、运行时间 |

数据流是这样的：

```text
sample_task --frame(32B)--> frame_queue --> tx_task --> UART --> 主机日志
     |                                                              |
     +-- heartbeat --> watchdog_task                        hexlog_to_raw.py
                                                                    |
                                                             serial_bridge.py
                                                                    |
                                                            本地面板（HTTP）
```

任务之间只通过队列传递数据，串口只在 `tx_task` 一处访问，所以输出不会互相穿插。这一点和真板上的做法一致。

## 看门狗怎么演示

`sample_task` 在模拟运行到 8 秒时故意卡死一次，模拟"传感器读取阻塞"：它进入 6 秒的延时并且不更新心跳。`watchdog_task` 发现心跳 1.5 秒没有变化，打印告警、把复位计数加一，然后写 Cortex-M 的 AIRCR 寄存器触发系统复位：

```c
*(volatile uint32_t *)0xE000ED0Cu = 0x05FA0004u;  /* VECTKEY | SYSRESETREQ */
```

复位后 CPU 重新从向量表启动，固件第二次运行。复位计数和启动次数放在链接脚本定义的 `.noinit` 段里，启动代码不清零它，所以能在复位后读出"这是第几次启动、看门狗救过几次"。

## 复现步骤

```powershell
cd <仓库根目录>

# 1. 编译两个固件镜像（看门狗演示版 + 稳定性测试版）
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\sim\build_firmware.ps1

# 2. 跑看门狗演示：90 秒，捕捉故障注入和复位恢复，并把帧回放到面板
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\sim\run_emulation.ps1 -Image watchdog -Seconds 90

# 3. 跑稳定性测试：600 秒，不做故障注入
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\sim\run_emulation.ps1 -Image stable -Seconds 600 -NoReplay
```

跑完后面板里会直接出现固件产生的数据，证据文件在 `docs/evidence/`：

| 文件 | 内容 |
| --- | --- |
| `watchdog-uart.log` | 固件原始串口输出 |
| `watchdog-frames.ehraw` | 还原出的协议帧 |
| `metrics-watchdog.json` | 帧数、错帧数、启动次数、复位次数 |
| `watchdog-telemetry.svg` | 由这些帧生成的曲线图 |

## 实测结果

见 [`test-report.md`](test-report.md) 的"仿真目标实测结果"一节。要点是：看门狗演示在 90 秒墙钟内完成 2 次"卡死 → 复位 → 恢复"，输出帧全部通过 CRC 校验，错帧为 0。

## 和真板相比还差什么

这段边界在对外介绍项目时要用得上，所以列清楚：

| 项目 | 仿真能证明 | 仿真不能证明 |
| --- | --- | --- |
| 代码 | 固件在 Cortex-M3 上编译、链接、运行 | 在具体 STM32 型号上的外设行为 |
| 实时性 | FreeRTOS 任务调度、tick、队列 | 真实中断延迟与抖动 |
| 串口 | 协议帧格式正确、可被主机解析 | 波特率误差、电平、抗干扰 |
| 传感器 | 数据链路与告警逻辑 | 真实传感器读数与标定 |
| 可靠性 | 看门狗可触发复位并恢复 | 电压跌落、复位电路、看门狗硬件行为 |
| 时间 | 以模拟时钟计的运行时长 | 真实墙钟下的稳定运行 |

另外要注意：仿真里 QEMU 执行速度慢于真实处理器，所以"模拟运行时间"明显小于墙钟时间。引用数据时要写清楚用的是哪一个口径。

## 迁移到真板的顺序

1. 用 CubeMX 建 STM32F103 工程，加入 `firmware/src` 和 `firmware/target/app_main.c` 的等价任务代码。
2. 用 HAL 重写 `board.c` 的三个函数：初始化、发送一个字节、发送字符串。
3. 保留 `startup.c` 的思路但不使用它（CubeMX 已生成启动文件），把向量表里的 SVC/PendSV/SysTick 指向 FreeRTOS 的处理器。
4. 传感器任务替换 `make_sample`，其余链路不变。
5. 用 `tools/serial_bridge.py --port COMx` 把真板数据接进同一个面板。
