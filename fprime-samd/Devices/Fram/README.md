# Fram — Infineon CY15B102QN / CY15V102QN SPI F-RAM

| Directory | Contents |
| --------- | -------- |
| `FramConfig/` | compile-time component configuration (`ClientPorts`, `MaxTransfer`) — project overridable |
| `Types/` | `Fram.Status`, device constants (`DEVICE_SIZE`, `HEADER_SIZE`) |
| `Ports/` | `Fram.Request`, `Fram.Reply` |
| `Components/FramDriver/` | the protocol driver, its SDD (`docs/sdd.md`) and native unit test |
| `Subtopology/` | `Fram.Subtopology` (driver + `Samd21.SpiDriver` host) and `FramSubtopologyConfig/` with the SPI host settings (SERCOM, pins, baud, mode, chip-select slot) |

Component configuration and SPI host configuration are deliberately separate: the
former describes the driver, the latter how a particular board wires the part.

Lives in fprime-samd because it binds to this library's asynchronous SPI port types
(`Samd21.SpiWriteRead` / `Samd21.SpiReply`) and host driver. See
`Components/FramDriver/docs/sdd.md` §4 for how a deployment integrates the subtopology.
