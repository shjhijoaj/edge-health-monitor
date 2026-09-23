# Telemetry Protocol v1

Frames are little-endian and use a fixed sample payload. The header is eight bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | sync `0xA5` |
| 1 | 1 | sync `0x5A` |
| 2 | 1 | protocol version `1` |
| 3 | 1 | message type `1` (sample) |
| 4 | 2 | sequence number |
| 6 | 2 | payload length, currently `22` |

The payload contains signed fixed-point values and status metadata:

| Offset | Size | Field | Unit |
| ---: | ---: | --- | --- |
| 8 | 4 | temperature | 0.01 deg C |
| 12 | 4 | humidity | 0.01 %RH |
| 16 | 4 | current | mA |
| 20 | 4 | vibration RMS | mg |
| 24 | 2 | status bit field | bit 0 sensor fault |
| 26 | 4 | uptime | seconds |
| 30 | 2 | CRC16/Modbus over offsets 0..29 | - |

The decoder is deliberately byte oriented. It can be called from a UART interrupt consumer, a USB CDC reader or a Linux serial reader without changing the framing logic. A bad frame is rejected before it reaches the rule engine.
