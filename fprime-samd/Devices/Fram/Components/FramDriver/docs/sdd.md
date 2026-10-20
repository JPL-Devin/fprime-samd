# Fram::FramDriver

Protocol driver for the Infineon CY15B102QN / CY15V102QN 2 Mbit SPI F-RAM.

## 1. Introduction

`Fram::FramDriver` turns a client's "read N bytes at offset" or "write N bytes at
offset" request into the SPI frames the F-RAM understands, issues them through an
asynchronous SPI host (`Samd21.SpiDriver`), and reports the outcome back to the
client later, in main context. It is a passive component with no commands, events,
telemetry or parameters: on a 16 KiB-RAM target every dictionary item costs
flash and downlink, and the only thing the driver has to say is the status of the
transaction, which travels on the reply port.

The device needs no busy polling: an F-RAM write completes at bus speed, so a
write is finished when its last byte has been clocked. What the protocol does
need is a `WREN` (write enable) opcode in its **own** chip-select cycle before
every `WRITE`, which is why a write is two SPI transactions.

## 2. Requirements

| Name | Description | Validation |
| ---- | ----------- | ---------- |
| REQ-FRAM-001 | The FramDriver shall accept a read request (offset, destination buffer) and read `buffer.size` bytes from the F-RAM at that offset using `READ` (03h) followed by three address bytes. | Unit Test, Hardware Test |
| REQ-FRAM-002 | The FramDriver shall accept a write request (offset, source buffer) and write `buffer.size` bytes at that offset using `WREN` (06h) in its own chip-select cycle followed by `WRITE` (02h), three address bytes and the data. | Unit Test, Hardware Test |
| REQ-FRAM-003 | The FramDriver shall complete every request asynchronously: the request port returns immediately and the completion is reported later on a reply port carrying the original offset, buffer and a status. | Unit Test |
| REQ-FRAM-004 | The FramDriver shall deliver each reply on the reply port whose index equals the request port index. | Unit Test |
| REQ-FRAM-005 | The FramDriver shall reject immediately, with no SPI traffic, any request with size 0, size greater than `Fram.Config.MaxTransfer`, or `offset + size` greater than `Fram.DEVICE_SIZE` (no address wrap). | Unit Test, Hardware Test |
| REQ-FRAM-006 | The FramDriver shall service one transaction at a time and reject a request that arrives while one is in flight with `BUSY`. | Unit Test |
| REQ-FRAM-007 | On a non-`SPI_OK` status from the SPI host in any phase the FramDriver shall abort the transaction (no `WRITE` after a failed `WREN`), reply `SPI_ERROR`, and return to idle. | Unit Test |
| REQ-FRAM-008 | The FramDriver shall build SPI frames in statically allocated staging storage of `MaxTransfer + HEADER_SIZE` bytes for each of TX and RX, with no dynamic allocation. | Inspection, Unit Test |
| REQ-FRAM-009 | On a completed read the FramDriver shall copy exactly `size` bytes, skipping the `HEADER_SIZE` header clocks, into the caller's buffer before replying. | Unit Test |
| REQ-FRAM-010 | The FramDriver shall be a passive component with synchronous input ports and shall not block on hardware. | Inspection |
| REQ-FRAM-011 | The FramDriver shall `FW_ASSERT` on an unexpected SPI reply: a reply while no SPI frame is in flight, or a reply carrying buffers other than its own staging buffers. | Unit Test |
| REQ-FRAM-012 | The FramDriver shall deliver client replies from the main (non-ISR) context: the SPI completion is latched in the ISR and the reply is issued on the next `activeIn` tick. | Unit Test |
| REQ-FRAM-013 | The FramDriver shall read the 9-byte device ID with `RDID` (9Fh) once at startup, after event reporting is available: from the first `activeIn` tick that finds the driver idle, never from `configure()` or the constructor. | Unit Test, Hardware Test |
| REQ-FRAM-014 | The FramDriver shall compare the ID with `Fram::EXPECTED_DEVICE_ID` (`FramSubtopologyConfig`, default CY15B102QN) and emit `FramDeviceIdMismatch` (WARNING_HI, both IDs as hex) on any difference; no event on a match. A mismatch does not block reads or writes. | Unit Test, Hardware Test |
| REQ-FRAM-015 | The ID read shall use the existing transaction machinery: the driver is `BUSY` to clients while it is in flight, the outcome is delivered in main context (REQ-FRAM-012), and an SPI failure takes the existing `SPI_ERROR` path, leaving the part unidentified with no event and no retry. | Unit Test |
| REQ-FRAM-016 | REQ-FRAM-001..012 are unchanged by the ID read; it reuses the READ/WRITE staging buffers (10 bytes). | Unit Test |

Status values: `OK`, `BUSY`, `OUT_OF_RANGE`, `TOO_LARGE`, `SPI_ERROR` (`Fram.Status`, see `Fram/Types/Types.fpp`).

## 3. Design

### 3.1 Overview

```
client ──read[i]/write[i]──▶ FramDriver ──spiWriteReadOut──▶ Samd21.SpiDriver ──▶ DMA
client ◀─readComplete[i]/──  FramDriver ◀──spiReplyIn (ISR)── Samd21.SpiDriver ◀── DMA ISR
          writeComplete[i]        ▲
                       activeIn (Svc.PassiveCycler, main context)
```

A request is validated, the frame is built in the TX staging buffer, and one
full-duplex SPI transaction is issued. The SPI completion arrives in the DMAC
interrupt. The ISR handler only latches the outcome (and, for a write, advances
the state machine); the client reply is delivered on the next `activeIn` tick.

### 3.2 Ports

| Kind | Name | Type | Notes |
| ---- | ---- | ---- | ----- |
| sync input | `read[ClientPorts]` | `Fram.Request` | read `buffer.size` bytes at `offset` |
| sync input | `write[ClientPorts]` | `Fram.Request` | write `buffer.size` bytes at `offset` |
| output | `readComplete[ClientPorts]` | `Fram.Reply` | `match readComplete with read` |
| output | `writeComplete[ClientPorts]` | `Fram.Reply` | `match writeComplete with write` |
| output | `spiWriteReadOut` | `Samd21.SpiWriteRead` | one full-duplex frame; TX and RX buffers are the same length |
| sync input | `spiReplyIn` | `Samd21.SpiReply` | **ISR context** |
| sync input | `activeIn` | `Svc.ActiveSched` | **required connection** to the deployment's `Svc.PassiveCycler`; returns `true` when work was done |

### 3.3 Configuration

Compile time, `Fram/FramConfig/FramConfig.fpp` (project-overridable):

| Constant | Default | Meaning |
| -------- | ------- | ------- |
| `Fram.Config.ClientPorts` | 2 | request/reply port pairs |
| `Fram.Config.MaxTransfer` | 128 | largest single transfer; RAM cost `2 * (MaxTransfer + 4)` bytes |

Device constants, `Fram/Types/Types.fpp`: `DEVICE_SIZE = 0x40000`, `ADDRESS_BYTES = 3`, `HEADER_SIZE = 4`.

The driver has no runtime configuration. The SPI host it talks to is configured by
`Fram.Subtopology` from `Fram/Subtopology/FramSubtopologyConfig/` (SERCOM, pins,
baud, mode, chip-select slot) — see §4.

### 3.4 Transaction behaviour

Frames (all bytes MSB first; `A2 A1 A0` is the 18-bit address in three bytes):

```
READ  : TX [03h A2 A1 A0 00 … 00]   RX [-- -- -- -- d0 … dN-1]
WREN  : TX [06h]                    RX [--]
WRITE : TX [02h A2 A1 A0 d0 … dN-1] RX [-- … --]
```

State machine:

```
IDLE ──read──▶ READ_IN_FLIGHT ──ISR: OK──▶ REPLY_PENDING ──activeIn──▶ IDLE (readComplete)
IDLE ──write─▶ WREN_IN_FLIGHT ──ISR: OK──▶ WRITE_PENDING ──activeIn──▶ WRITE_IN_FLIGHT ──ISR: OK──▶ REPLY_PENDING ──activeIn──▶ IDLE (writeComplete)
any *_IN_FLIGHT ──ISR: error──▶ REPLY_PENDING(SPI_ERROR)
```

The `WRITE` is launched from `activeIn`, not from inside the `WREN` completion
callback: that callback runs in the DMAC ISR before `Samd21.DmaDriver` has
finished its own channel bookkeeping, and queuing the next transaction from there
is a documented-unsafe pattern (it orphaned the descriptor chain on hardware). The
cost is at most one cycler tick of extra write latency.

Rejections (`BUSY`, `OUT_OF_RANGE`, `TOO_LARGE`) are delivered **synchronously
from inside the request call**, on the matching reply port, with no SPI traffic.
Clients must therefore commit their own state before issuing a request (see
`CuriosityReference/FramTester/docs/sdd.md` §3.5 for the client contract).

### 3.4.1 Startup device identification (REQ-FRAM-013..016)

The first `activeIn` tick that finds the driver `IDLE` (normally the first tick after
`configure()`, when the SPI host and the event ports are connected) stages one `RDID`
frame — `9Fh` followed by nine `00h` dummy bytes, 10 bytes under one chip select — in the
READ/WRITE staging buffers and clocks it through the normal `READ_IN_FLIGHT` state with
`Operation::IDENTIFY`. Clients see `BUSY` until it completes. The outcome is latched in the
SPI ISR like any frame and consumed on the next tick in main context: on `OK` bytes
`rx[1..9]` are compared with `Fram::EXPECTED_DEVICE_ID`; a difference logs
`FramDeviceIdMismatch` with both IDs rendered as 18 upper-case hex characters. Any other
outcome (SPI failure) returns the driver to `IDLE` silently: the part is simply not
identified, and no retry port exists. The read happens exactly once per boot
(`m_identified`); the event is never raised from `configure()`, the constructor or ISR
context, so it cannot fire before the port connections are made, and the autocoded
`log_WARNING_HI_*` call is itself guarded by `isConnected` on the event ports.

Expected ID (`FramDeviceIdConfig.hpp`, part of `FramSubtopologyConfig`), stored in wire
order: Infineon datasheet 002-26764 Rev. *D (CY15B102QN/CY15V102QN, 2022-09-12), section
"Read device ID (RDID, 9Fh)" and the "Ordering information" table, give the CY15B102QN ID as
`7F7F7F7F7F7FC22A00` MSB first — six `7Fh` JEDEC continuation bytes, manufacturer `C2h`,
product ID `2A00h` — and state that byte 0 (least significant) shifts out first, so the
driver receives `00 2A C2 7F 7F 7F 7F 7F 7F` (verified on the Curiosity Nano bench: the
mismatch event reports `got 002AC27F7F7F7F7F7F` when the MSB-first value is configured).
The CY15V102QN reads `…C22A04`. A different fitted part only needs this constant changed.
The strings in the event are 18 characters; a deployment whose `FW_LOG_STRING_MAX_SIZE` is
smaller (the SAMD21 reference uses 2) sees them truncated.

### 3.5 Error handling

- Any `Drv::SpiStatus` other than `SPI_OK` in any phase → `SPI_ERROR`; for a write
  this includes a failed `WREN`, after which no `WRITE` is issued.
- Validation failures never reach the SPI bus.
- Protocol violations by the SPI host (reply while idle, foreign buffers) are
  `FW_ASSERT`s: they indicate a wiring or driver bug, not a runtime condition.
- A reply whose completion output port is unconnected is dropped; the subtopology
  wiring (`match`) and the deployment are responsible for connecting both ports of
  every used index.

### 3.6 Completion delivery

`spiReplyIn` runs in ISR context and touches only `m_state`/`m_status` (both
`volatile`) and the staging buffers. `activeIn` runs in main context, copies read
data into the client buffer, and invokes the client's completion port. The
`Svc.ActiveSched` return value lets `Svc.PassiveCycler` know whether the tick did work.

## 4. Integration

`Fram.Subtopology` (`Fram/Subtopology/Subtopology.fpp`) instantiates `framDriver`
and the `Samd21.SpiDriver` instance `spiDriver`, connects them, and configures
`spiDriver` in its `configComponents` phase from `FramSubtopologyConfig.hpp`. A
deployment:

1. adds `./lib/fprime-samd` to `library_locations` and `fprime-samd_Devices_Fram_Subtopology` to its topology dependencies;
2. includes `fprime-samd/Devices/Fram/Subtopology/FramSubtopologyTopologyDefs.hpp` from its `TopologyDefs.hpp`;
3. imports `Fram.Subtopology` into its topology and connects:
   - `cycler.cycleOut -> Fram.framDriver.activeIn`
   - `Fram.spiDriver.dmaTransactionOut[MOSI|MISO] <-> dmaDriver` (two DMA channels)
   - `Fram.spiDriver.chipSelectGpioOut[Fram.SubtopologyConfig.SPI_SLOT] -> <cs gpio>.gpioWrite`
   - its client(s) to `Fram.framDriver.read/write` and the matching completion ports;
4. configures the chip-select GPIO as an output **before** `configComponents()` — `SpiDriver::configure()` deasserts the chip select through the GPIO port, and an unconfigured `Samd21.GpioDriver` drops the write.

Default SPI host settings (`FramSubtopologyConfig.hpp`): SERCOM1, MOSI PA16 (PAD0), SCK PA17 (PAD1), MISO PA19 (PAD3), software chip select, SPI mode 0, 4 MHz. A deployment that wires the part differently overrides that configuration module.

The driver binds to `Samd21.SpiWriteRead`/`Samd21.SpiReply`, the asynchronous SPI interface of this library, which is why it lives under `fprime-samd/Devices` rather than in a platform-neutral package.

## 5. Unit tests

`test/ut/` (`fprime-util check` natively) covers every interface: read/write framing and
buffer identity, same-index completion on all client ports, every rejection
(zero size, oversize, end-of-device, overflow, busy), SPI failure in each phase
including the WREN abort, synchronous rejection by the SPI host, replies only on
`activeIn`, client re-entrancy from the completion callback, 1000 randomized
transactions, the assertion on a stray SPI reply, and the startup identification
(matching ID with no event, mismatching ID with the event strings, SPI failure on the
`RDID` frame returning to `IDLE`, clients rejected `BUSY` while it is in flight).

## 6. Tested configurations

| Target | SPI host | Result |
| ------ | -------- | ------ |
| SAMD21 Curiosity Nano + CY15B102QN, SERCOM1 @ 4 MHz mode 0, software CS PA18 | `Samd21.SpiDriver` + `Samd21.DmaDriver` | Hardware tested via `CuriosityReference.FramTester` |
