# Hardware Wiring

The first board target is STM32F103C8T6 or NUCLEO-G071RB. All sensor modules should use the board's 3.3 V rail. Do not connect a motor or fan directly to an MCU GPIO; use a MOSFET driver and a flyback diode.

| Module | MCU pin suggestion | Bus | Purpose |
| --- | --- | --- | --- |
| MPU6050 | PB6/PB7 | I2C1 | vibration and acceleration |
| BME280 | PB6/PB7 | I2C1 | temperature and humidity |
| INA219 | PB6/PB7 | I2C1 | supply current and voltage |
| OLED SSD1306 | PB6/PB7 | I2C1 | local status display |
| USB-UART or RS485 transceiver | PA9/PA10 | USART1 | telemetry link |
| buzzer/LED | GPIO output | - | local fault indication |

The three I2C sensors use different addresses. Confirm the actual breakout board address before powering the bus. The firmware port should configure pull-up resistors, check `HAL_I2C_IsDeviceReady`, and set the status bit when a read times out.

## Porting order

1. Bring up one sensor and print a raw reading over UART.
2. Convert readings to the fixed-point fields described in `docs/protocol.md`.
3. Call `eh_encode_sample` from a transmit task.
4. Run the host gateway against captured UART bytes.
5. Add the OLED and watchdog only after the telemetry path is stable.
