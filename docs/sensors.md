# 传感器层

这一层解决的问题是：怎么让"数据链路"里的采集部分成为真实代码，而不是一个写死的数。

## 分层

```text
sensor_task (FreeRTOS)
        |
        v
sensor_driver_t        统一驱动接口：init / read，输出工程单位
        |
        v
i2c_bus_t              三个操作：probe / write / read
        |
        +--> i2c_bus_simulated()   寄存器级器件模型（仿真与主机测试）
        |
        +--> i2c_bus_hal()         STM32 HAL 实现（真板）
```

驱动层只认 `i2c_bus_t` 的三个函数。换成真硬件时，改的是总线实现，不是驱动逻辑。

## 文件

| 文件 | 作用 |
| --- | --- |
| [`firmware/target/sensors/sensor.h`](../firmware/target/sensors/sensor.h) | 读数结构与驱动接口 |
| [`firmware/target/sensors/sensor.c`](../firmware/target/sensors/sensor.c) | 后端选择（默认 I2C 驱动栈） |
| [`firmware/target/sensors/sensor_i2c.c`](../firmware/target/sensors/sensor_i2c.c) | BME280 / INA219 / MPU6050 寄存器级驱动 |
| [`firmware/target/sensors/i2c_bus.h`](../firmware/target/sensors/i2c_bus.h) | 总线抽象与器件地址 |
| [`firmware/target/sensors/i2c_bus_sim.c`](../firmware/target/sensors/i2c_bus_sim.c) | 三个器件的寄存器模型 |
| [`firmware/target/sensors/i2c_bus_hal.c`](../firmware/target/sensors/i2c_bus_hal.c) | STM32 HAL 总线实现（模板） |
| [`firmware/target/sensors/sensor_sim.c`](../firmware/target/sensors/sensor_sim.c) | 直接合成数值的后端，用于不做器件模型时的快速演示 |
| [`tests/test_sensors.c`](../tests/test_sensors.c) | 主机侧驱动与故障路径测试 |

## 三个器件怎么用

| 器件 | 地址 | 读取内容 | 换算 |
| --- | --- | --- | --- |
| BME280 | `0x76` | 标定寄存器 `0x88`、`0xA1`、`0xE1`；数据寄存器 `0xF7` 起 8 字节 | 温度用数据手册整数补偿（输出 0.01°C）；湿度用 H1/H2 的线性近似 |
| INA219 | `0x40` | 电流寄存器 `0x04`；初始化时写标定寄存器 `0x05` | 1 LSB = 0.1 mA，除以 10 得到 mA |
| MPU6050 | `0x68` | 加速度寄存器 `0x3B` 起 6 字节；初始化时写 `0x6B`（唤醒）与 `0x1C`（±2g） | 整数开方求加速度模长，±2g 下 16384 LSB/g，换算成 mg 后取与 1g 的偏差 |

温度换算用的是公开的整数补偿算法，模型里的 raw 值经过这个算法后落在 61.33°C 到 82.00°C 之间，每个 24 采样周期跨过一次 80°C 告警线，所以协议、网关规则和面板告警都能被真实触发。

## 后端怎么切换

固件默认使用 I2C 驱动栈加寄存器模型。想用直接合成数值的后端：

```bash
arm-none-eabi-gcc ... -DEHM_SENSOR_BACKEND_SIM=1
```

想在真板上使用 HAL 总线：

```bash
arm-none-eabi-gcc ... -DEHM_USE_HAL_I2C=1 -DEHM_HAL_I2C_HANDLE=hi2c1
```

此时把 `sensor_i2c.c` 里的 `g_bus = i2c_bus_simulated();` 换成 `g_bus = i2c_bus_hal();`（或者让 `sensor.c` 根据编译开关选择），驱动逻辑本身不用动。

## 主机上就能测

因为总线和器件模型都是普通 C 代码，驱动可以在电脑上跑：

```powershell
ctest --test-dir build -C Release -R sensor_drivers --output-on-failure
```

测试覆盖三件事：正常读数的量程与单位、器件离线时是否置状态位、后端选择接口。它不需要任何硬件，也不需要 FreeRTOS。

## 已知简化

这几点在对外介绍时要讲清楚，避免夸大：

| 项目 | 现状 | 真板上的差别 |
| --- | --- | --- |
| 湿度换算 | H1/H2 线性近似，不是完整 12 位公式 | 完整公式在边界温度下精度更高 |
| 振动指标 | 单次采样的加速度模长与 1g 的偏差，不是多采样 RMS | 真板上应按采样窗口计算 RMS |
| INA219 标定 | 固定写 4096，1 LSB = 0.1 mA | 需要按实际分流电阻重算 |
| 器件模型 | 寄存器值由采样时钟推导 | 真实器件有自己的量测和噪声 |
| 采样时序 | 仿真时钟（QEMU 比真实处理器慢） | 真板是 100 ms 硬周期 |
