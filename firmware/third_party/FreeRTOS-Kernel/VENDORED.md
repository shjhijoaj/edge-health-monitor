# 内置的 FreeRTOS 内核

这里放的是本仓库需要的那部分 FreeRTOS 内核源码，目的是让仓库做到"克隆即用"：不需要联网下载，也不需要手动解压。

## 来源

| 项目 | 内容 |
| --- | --- |
| 上游仓库 | <https://github.com/FreeRTOS/FreeRTOS-Kernel> |
| 分支 | `main` |
| 获取时间 | 2026-09-23 |
| 许可证 | MIT，见本目录的 [`LICENSE.md`](LICENSE.md) |
| 来源压缩包 | `https://codeload.github.com/FreeRTOS/FreeRTOS-Kernel/zip/refs/heads/main` |

## 复制了什么

| 路径 | 用途 |
| --- | --- |
| `tasks.c`、`queue.c`、`list.c` | 内核本体：任务调度与队列 |
| `include/` 全部头文件 | 内核对外接口 |
| `portable/GCC/ARM_CM3/port.c`、`portmacro.h` | Cortex-M3 的 GCC 端口 |
| `portable/MemMang/heap_4.c` | 动态内存分配实现 |
| `LICENSE.md` | 上游许可证原文 |

没有复制的内容：其他处理器端口、`event_groups.c`、`stream_buffer.c`、`timers.c`、单元测试与示例。本固件没有使用这些功能，`FreeRTOSConfig.h` 里对应的开关也是关闭的。

## 怎么更新

```powershell
cd <仓库根目录>
Invoke-WebRequest -Uri 'https://codeload.github.com/FreeRTOS/FreeRTOS-Kernel/zip/refs/heads/main' -OutFile .tools\FreeRTOS-Kernel.zip
Expand-Archive .tools\FreeRTOS-Kernel.zip -DestinationPath .tools -Force

$src = '.tools\FreeRTOS-Kernel-main'
$dst = 'firmware\third_party\FreeRTOS-Kernel'
Copy-Item "$src\include\*" "$dst\include" -Force
Copy-Item "$src\tasks.c","$src\queue.c","$src\list.c","$src\LICENSE.md" $dst -Force
Copy-Item "$src\portable\GCC\ARM_CM3\port.c","$src\portable\GCC\ARM_CM3\portmacro.h" "$dst\portable\GCC\ARM_CM3" -Force
Copy-Item "$src\portable\MemMang\heap_4.c" "$dst\portable\MemMang" -Force
```

更新后重新编译固件并跑一次仿真，确认 `docs/evidence` 里的错帧仍然是 0。

## 覆盖方式

构建脚本默认使用这个目录，也可以指向别处的内核：

```powershell
pwsh -File tools/sim/build_firmware.ps1 -FreeRtosRoot <另一份内核目录>
```

```bash
FREERTOS_ROOT=<另一份内核目录> bash tools/sim/build_firmware.sh
```
