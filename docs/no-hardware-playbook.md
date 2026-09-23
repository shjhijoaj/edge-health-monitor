# 没有开发板时的验证路线

这份文档回答一个问题：手上没有 STM32 板子，怎么让这个项目的证据尽量硬。

## 先说清楚能到哪一步

项目分成三层，缺板子只影响最下面那一层：

| 层次 | 内容 | 现在有没有证据 |
| --- | --- | --- |
| 协议层 | 帧格式、CRC16、逐字节解码状态机 | 有，主机侧完整回归 |
| 网关层 | 规则引擎、CSV 落盘、告警 | 有，23 条断言 + 端到端跑通 |
| 面板层 | 实时展示、在线判定、接口写入 | 有，截图和接口返回都在 |
| 固件层 | FreeRTOS 多任务、Cortex-M3 目标运行、看门狗复位恢复 | 有，QEMU 仿真实测 |
| 硬件层 | 真实传感器读数、真实时序、真实抗干扰 | **没有** |

所以缺的不是"项目能不能讲"，而是"硬件那一句话能不能写成实测"。解决办法有三个，按投入从低到高。

## 路线一：在线 Cortex-M 仿真（0 成本，1-2 小时）

浏览器里就能跑真实固件，不需要装任何东西。Wokwi 一类的在线仿真器支持 STM32 和 ESP32，能编译你的 C 代码并输出串口。具体可用板卡以网站当前的列表为准。

操作顺序：

1. 新建一个 STM32 或 ESP32 项目。
2. 把 [`firmware/include/eh_protocol.h`](../firmware/include/eh_protocol.h) 和 [`firmware/src/eh_protocol.c`](../firmware/src/eh_protocol.c) 原样贴进项目。这两个文件不依赖 HAL，能直接编译。
3. 写一个主循环，把采样值打包成帧，然后用串口以十六进制打印。可以直接用下面这段：

```c
#include "eh_protocol.h"
#include <stdio.h>
#include <string.h>

static void print_frame(const uint8_t *frame, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        printf("%02X", frame[i]);
        if (i + 1 < length) printf(" ");
    }
    printf("\n");
}

int main(void) {
    uint16_t sequence = 0;
    while (1) {
        eh_sample_t sample = {
            .temperature_centi_c = 6300 + (int32_t)(sequence % 20) * 100,
            .humidity_centi_pct = 4860,
            .current_ma = 920,
            .vibration_rms_mg = 130,
            .status = 0,
            .uptime_s = sequence * 2,
        };
        uint8_t frame[EH_MAX_FRAME_SIZE];
        size_t frame_size = 0;
        if (eh_encode_sample(&sample, ++sequence, frame, sizeof(frame), &frame_size)) {
            print_frame(frame, frame_size);
        }
        delay(1000);   /* 换成你所用平台的延时函数 */
    }
}
```

4. 运行，把串口窗口里的输出复制出来存成 `serial-log.txt`。
5. 把文本日志还原成可回放的帧文件：

```powershell
python tools\hexlog_to_raw.py --input serial-log.txt --output data\capture-wokwi.ehraw --dump
```

6. 回放给本地面板，`bad frames` 必须是 0：

```powershell
python tools\serial_bridge.py --replay data\capture-wokwi.ehraw
```

7. 截图存档：仿真界面、串口输出、面板显示各一张，放进 `docs/evidence/`。

这样你就有了一条真实证据：固件代码在 Cortex-M 目标上编译并运行，输出的帧被独立实现的主机解码器完整还原。对外说明时必须讲清这是仿真器，不要把仿真读数说成传感器实测。

## 路线二：本地跑真实固件（已实现，约 20 分钟）

这条路已经在仓库里落地：ARM 交叉编译 + QEMU 模拟 Cortex-M3 + FreeRTOS 多任务，全流程脚本化，结果写进 `docs/evidence/`。完整说明见 [`emulation.md`](emulation.md)。

```powershell
# 一次性安装工具链
C:\msys64\usr\bin\pacman.exe -S --noconfirm --needed `
    mingw-w64-ucrt-x86_64-arm-none-eabi-gcc mingw-w64-ucrt-x86_64-qemu

# 编译固件
pwsh -File tools\sim\build_firmware.ps1

# 看门狗演示 + 回放面板
pwsh -File tools\sim\run_emulation.ps1 -Image watchdog -Seconds 90

# 稳定性测试
pwsh -File tools\sim\run_emulation.ps1 -Image stable -Seconds 600 -NoReplay
```

能拿到什么：真实固件在 Cortex-M3 上运行的证据、FreeRTOS 任务调度证据、看门狗复位并恢复的证据、帧级串口日志。拿不到什么：真实传感器读数、真实中断延迟、电气特性。这条边界在 [`emulation.md`](emulation.md) 里列了对照表。

## 路线三：最低成本买一块真板（1-2 天）

如果只想花最少的钱并拿到"实测"两个字，最小配置是：

| 物件 | 参考价格 | 说明 |
| --- | --- | --- |
| STM32F103C8T6 最小系统板 | 10-20 元 | 最便宜的 Cortex-M3 入门板 |
| USB 转 TTL 串口模块 | 5-10 元 | 没有它串口连不上电脑 |
| 传感器（可选） | 3-15 元 | DS18B20 或 BME280 最容易上手 |

如果选 ESP32-C3 这类自带 USB 串口的板子，可以省掉 USB 转 TTL。国内电商同城或次日达通常 1-2 天，秋招窗口通常持续到 11 月，两天并不算来不及。真板到货后的接入步骤见 [`local-operation.md`](local-operation.md) 的"接入真实设备"一节。

## 能力边界：能证明什么，不能证明什么

现在有证据支撑、经得起追问的：

> 设计并实现设备遥测协议：自定义定长帧结构、CRC16/Modbus 校验和逐字节解码状态机，错帧可被识别并重新同步；用主机回归测试覆盖编码往返、校验失败路径和采样算法。基于 C++17 实现边缘网关，完成遥测落盘、四类阈值告警和 JSON 消息生成，使用 CMake 与 CTest 建立可重复的构建测试流程，并提供本地面板完成端到端可视化验证。

加了仿真验证之后可以补一句：

> 固件层在 Cortex-M 目标（仿真环境）上完成编译与运行验证，输出帧经独立实现的主机解码器交叉校验，24 帧全部通过。

没有证据支撑、不要对外声称的内容：把模拟数据说成"传感器实测数据"，把拼 JSON 的类说成"接入 MQTT 协议"，把仿真运行说成"真板实测"。这几条是最容易被追问倒的地方。

## 被问到"为什么没有硬件验证"时怎么答

照实说，并且把话题引到你真正做过的部分：

> 这个项目我做的是协议层和网关层。硬件采样层我留了标准的适配接口，固件里除了 HAL 调用之外的部分都是可移植的 C 代码，所以主机上就能把协议和告警逻辑完整跑通。板子还没到位，我用仿真环境验证了固件编译和帧输出，并且用 Python 独立写了一份解码器做交叉验证，两边结果一致，说明协议定义和实现是自洽的。拿到板子之后主要工作是替换采样来源和实测时序。

这段回答展示了三件事：你知道分层边界、你有可复现的验证手段、你清楚还差什么。比含糊地说"都做完了"要安全得多。

## 需要留的证据清单

| 证据 | 用途 |
| --- | --- |
| 编译和测试输出 | 证明代码能构建、测试能过 |
| 面板截图（在线 + 离线两种状态） | 证明功能真的在跑 |
| 串口十六进制日志 | 证明帧是协议产物，不是页面里的假数据 |
| 回放结果 `bad frames: 0` | 证明编码器与解码器一致 |
| 仿真或真板运行截图 | 证明固件在目标平台上跑过 |
| 连续运行记录 | 证明稳定性，哪怕是 30 分钟 |

## 建议的时间分配

| 时间 | 做什么 |
| --- | --- |
| 今晚 | 跑 `run_emulation.ps1`，确认 `bad frames: 0`，把证据文件留着 |
| 明天 | 按上面的口径整理对外介绍，准备"没有硬件"的问答 |
| 有 1-2 小时空档 | 加传感器仿真或把仿真接进持续集成，做成脚本化回归 |
| 两天内 | 下单最便宜的板子，到货后补真板实测数据 |
