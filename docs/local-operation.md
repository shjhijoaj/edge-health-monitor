# 本地运行指南

## 链路概览

实验室与小型机房里的电机、电源、泵、测试台通常没有联网能力，故障前往往先出现温度上升、电流变大、振动加剧等征兆，人工巡检难以发现。

这个项目做的是这样一条链路：

```text
传感器 -> STM32 固件（C） -> 串口帧 -> 边缘网关（C++） -> CSV + 告警 -> 本地面板
```

设备端负责采样与打包，网关负责校验、落盘与异常判定，面板负责呈现当前状态。主机模拟器可以扮演设备端，因此整条链路可以直接在一台电脑上运行。

## 启动步骤

编译（只需执行一次）：

```powershell
cd <仓库根目录>
cmake -S . -B build
cmake --build build --config Release
```

运行测试：

```powershell
ctest --test-dir build -C Release --output-on-failure
```

启动模拟设备与监控面板：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/run_local.ps1
```

浏览器打开 <http://127.0.0.1:8765/>，可以看到设备在线状态、温度趋势、告警列表和最近采样。按 `Ctrl+C` 停止，脚本会自动结束模拟设备进程。

## 常用参数

模拟设备支持这些开关：

```powershell
# 持续采集，每 1 秒一条
build\Release\edge_health_sim.exe --loop --interval-ms 1000 --raw data\capture.ehraw data\telemetry.csv

# 只跑 24 条后退出，用于快速验证
build\Release\edge_health_sim.exe data\telemetry.csv
```

`--raw` 会把协议原始帧写到文件，方便离线分析和回放。

## 接入真实设备

### 方式一：串口直连

STM32 通过 USB 转串口接到电脑后：

```powershell
pip install pyserial
python tools\serial_bridge.py --port COM3 --baud 115200
```

桥接工具会解析设备发来的帧、校验 CRC，然后写入面板。设备只要按照 [`docs/protocol.md`](protocol.md) 的格式发送数据即可。

### 方式二：HTTP 接口

任何能发网络请求的设备或脚本都可以直接推送一条采样：

```powershell
$body = @{
    sequence = 9001
    temperature_c = 88.5
    humidity_pct = 51.2
    current_ma = 2700
    vibration_rms_mg = 640
    status = 1
    uptime_s = 4200
} | ConvertTo-Json

Invoke-RestMethod -Method Post -Uri http://127.0.0.1:8765/api/ingest -Body $body -ContentType 'application/json'
```

字段缺失或数值非法时接口返回 400，不会写入脏数据。

### 方式三：离线回放

先用模拟器录一段原始帧，再回放给面板，这条路径不需要任何硬件：

```powershell
python tools\serial_bridge.py --replay data\capture.ehraw
```

## 面板接口

| 接口 | 方法 | 用途 |
| --- | --- | --- |
| `/api/health` | GET | 服务存活检查 |
| `/api/summary` | GET | 设备在线状态、采样数、告警数、极值 |
| `/api/telemetry` | GET | 最近 100 条采样和告警 |
| `/api/ingest` | POST | 写入一条采样 |

在线判定规则：最近 5 秒内有新数据算在线，超过 5 秒算离线，这样能区分“设备正常”和“设备掉线”。

## 数据文件

| 文件 | 内容 |
| --- | --- |
| `data/telemetry.csv` | 网关写入的采样记录，可直接用 Excel 打开 |
| `data/capture.ehraw` | 协议原始帧，用于回放和排查 |
| `data/simulator.log` | 模拟设备的控制台输出 |

`data/` 下的运行时文件不会进入版本管理，删掉不影响程序运行。
