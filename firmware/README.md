# Firmware Port

`firmware/src` intentionally has no STM32 or FreeRTOS include. It is the portable part that can be tested on a PC and linked into a CubeMX project.

In the board project, keep the following responsibilities separate:

- HAL drivers read the sensor registers and return scaled integers.
- A FreeRTOS sampling task fills `eh_sample_t` every 100 ms.
- A transmit task calls `eh_encode_sample` and writes the frame to UART/RS485.
- The watchdog task observes sensor timeout and transport error counters.

This boundary is useful in an interview: the protocol and math are deterministic C code, while the MCU-specific code stays small and easy to replace.
