# Architecture

## Data path

```text
sensor HAL -> sample task -> eh_sample_t -> UART/RS485 frame
                                      |
                                      v
                     串口桥接 / C++ 网关字节解码
                                      |
                     +----------------+----------------+
                     v                                 v
                CSV 遥测落盘                        告警规则
                     |
                     v
              本地面板（HTTP + 浏览器）
```

The current host build generates samples in `gateway/src/main.cpp`. A board port replaces only that generator and the transport adapter. The protocol and rule behavior remain the same, which lets a host test catch framing regressions before hardware debugging.

## 三层可执行实现

| 层 | 位置 | 运行形态 |
| --- | --- | --- |
| 固件 | `firmware/target/` + `firmware/src/` | ARM 交叉编译，QEMU 模拟 Cortex-M3，FreeRTOS 四任务 |
| 传感器 | `firmware/target/sensors/` | 寄存器级驱动 + I2C 总线抽象；器件模型可换为 STM32 HAL |
| 网关 | `gateway/src/` + `tools/serial_bridge.py` | 主机进程，解析帧、落盘、告警；桥接工具负责串口或网络输入 |
| 面板 | `dashboard/` + `tools/dashboard_server.py` | 本机 HTTP 服务，实时展示与告警 |

三层共用同一个协议定义（`docs/protocol.md`）和同一份阈值配置（`config/thresholds.cfg`）。

传感器层的设计说明见 [`sensors.md`](sensors.md)：驱动只依赖三个总线操作，因此同一份驱动代码在仿真、主机测试和真实 STM32 上都能运行。

## 数据入口

面板支持三条入口，它们最终写入同一份 CSV 并使用同一套告警规则：

| 入口 | 适用场景 |
| --- | --- |
| C++ 模拟器 | 没有硬件时的完整演示和回归测试 |
| `tools/serial_bridge.py` | 真实设备通过串口发帧，桥接工具解码后写入 |
| `POST /api/ingest` | 已有的脚本、网关或第三方系统直接推送采样 |

设备在线状态由数据新鲜度决定：最近 5 秒内有新数据算在线，超过 5 秒算离线。这样面板不会把“设备掉线”误显示成“设备正常”。

## Firmware task model

The planned FreeRTOS port has four small tasks:

| Task | Period | Responsibility |
| --- | ---: | --- |
| `sample_task` | 100 ms | Read BME280/INA219/MPU6050 and fill a sample structure |
| `display_task` | 250 ms | Refresh the OLED and local status LED |
| `tx_task` | event driven | Encode a sample and send it through UART/RS485 |
| `health_task` | 1 s | Feed the watchdog and record sensor timeout counters |

The interrupt handlers only move bytes into a ring buffer or release a semaphore. They do not perform CRC or sensor I/O. This keeps interrupt latency bounded and makes the same C protocol library usable in the host tests.

## Failure handling

- CRC mismatch: discard the frame, keep the decoder synchronized, and increment a transport counter.
- Invalid version or payload length: reset the frame state immediately.
- Sensor timeout: set the status bit in the next sample instead of sending a stale value.
- Gateway storage failure: fail at startup with the target path, so an operator does not mistake a running process for persisted telemetry.
- Threshold crossing: create an alert for the current sequence; the caller can add a debounce window when connecting a real notification service.
