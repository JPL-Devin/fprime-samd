# Samd21::SpiDriver

Driver for the SAMD21 SERCOM SPI peripheral in host mode, with DMA data movement and
asynchronous completion.

## 1. Introduction

`Samd21::SpiDriver` presents the `Samd21.AsyncSpi` interface: `Samd21.SpiPorts`
request ports (`SpiWriteRead`, one per attached device / chip select) with matching
`SpiReply` completion ports. A request clocks one full-duplex frame — the write and
read buffers must be the same length — using two `Samd21.DmaDriver` channels (MISO
armed first, then MOSI, which starts the clock). The completion is reported when
both DMA channels have finished.

Chip select is either driven by the SERCOM (`HardwareChipSelect::ENABLED`, pad from
`DataOutPinout`) or by software through `chipSelectGpioOut[port]` around each frame.

The register-level code lives behind a tiny HAL (`SpiDriverHardware.hpp`):
`SpiDriverHardware.cpp` for the SAMD21 target, `SpiDriverHardwareStub.cpp` (which
records the configuration it was given) for native builds and the unit test. CMake
selects on `FPRIME_PLATFORM`.

## 2. Requirements

| Name | Description | Validation |
| ---- | ----------- | ---------- |
| SPI-CFG-001 | `configure()` shall program the SERCOM for SPI host mode once with the requested data order, clock polarity/phase, pad routing, run-in-standby and chip-select mode, and mark the driver configured. | Unit Test, Hardware Test |
| SPI-CFG-002 | With software chip select, `configure()` shall deassert (drive HIGH) every connected chip-select GPIO before the peripheral is enabled. | Unit Test |
| SPI-CFG-003 | With hardware chip select, the driver shall never drive `chipSelectGpioOut`. | Unit Test |
| SPI-CFG-004 | Reconfiguring an already configured driver shall assert. | Unit Test |
| SPI-CFG-005 | The BAUD register shall be computed as `fref / (2 * fsck) - 1`, rounded so the real SCK never exceeds the requested rate. | Unit Test |
| SPI-CFG-006 | `calculateBaud` shall assert on a zero rate, a rate above `fref / 2`, or a rate too slow for the 8-bit register, before any arithmetic that could overflow. | Unit Test |
| SPI-REQ-001 | A request issued before `configure()` shall assert. | Unit Test |
| SPI-REQ-002 | A request whose write and read buffers differ in size shall assert (full duplex). | Unit Test |
| SPI-XFER-001 | A request shall assert the port's chip select (software mode) and queue the MISO then the MOSI DMA transaction against the SERCOM DATA register. | Unit Test, Hardware Test |
| SPI-XFER-002 | The reply shall be emitted on the requesting port once both DMA channels complete, in either order, after the chip select is deasserted. | Unit Test, Hardware Test |
| SPI-XFER-003 | One transaction at a time: an overlapping request on any port shall be answered synchronously with `SPI_OTHER_ERR` and generate no DMA or chip-select traffic. | Unit Test |
| SPI-XFER-004 | With hardware chip select the transaction shall not drive the GPIO ports. | Unit Test |
| SPI-XFER-005 | A DMA completion with nothing in flight, or a duplicate completion of one channel, shall assert. | Unit Test |
| SPI-ERR-001 | `configure()` shall register for the SERCOM interrupt and enable only its `ERROR` source (receive buffer overflow, `STATUS.BUFOVF`), at the same NVIC priority as the DMAC interrupt. | Unit Test, Hardware Test |
| SPI-ERR-002 | On a receive overrun during a transaction the driver shall abort every DMA stream still outstanding via `dmaTransactionAbortOut`, acknowledge the hardware (wait for the shifter to go idle, clear `BUFOVF`, drain `DATA`, clear `INTFLAG.ERROR`), deassert the chip select (software mode) and reply on the requesting port with `SPI_OTHER_ERR` and the caller's buffers. | Unit Test, Hardware Test |
| SPI-ERR-003 | After an overrun reply the driver shall accept a new transaction immediately. | Unit Test, Hardware Test |
| SPI-ERR-004 | A SERCOM interrupt without `INTFLAG.ERROR`, or an overrun with no transaction in flight, shall not abort, reply or alter the transaction state (the latter is acknowledged). | Unit Test |

## 3. Design

### 3.1 Ports

| Kind | Name | Type | Notes |
| ---- | ---- | ---- | ----- |
| sync input | `SpiWriteRead[SpiPorts]` | `Samd21.SpiWriteRead` | from `Samd21.AsyncSpi` |
| output | `SpiReply[SpiPorts]` | `Samd21.SpiReply` | **ISR context** on success; synchronous on busy reject |
| output | `dmaTransactionOut[DmaChannel.N]` | `Dma.Transaction` | `MOSI`, `MISO` |
| output | `dmaTransactionAbortOut[DmaChannel.N]` | `Fw.Signal` | abort a stream after a receive overrun; **ISR context** |
| sync input | `dmaReplyIn[DmaChannel.N]` | `Dma.TransactionReply` | **ISR context** |
| output | `chipSelectGpioOut[SpiPorts]` | `Drv.GpioWrite` | software chip select only |

### 3.2 Configuration (`configure()`)

`configure(sercom, baud_rate_khz, DataOrder, ClockPolarity, ClockPhase, DataInPinout,
DataOutPinout, RunInStandby, HardwareChipSelect)` — called once from a `configComponents`
phase. The caller routes the pads (`Samd21::PinMux::configure`) and configures the
chip-select GPIO as an output **before** this call, because `configure()` deasserts
every connected software chip select. The GCLK core clock (`F_CPU`) is the baud reference.

### 3.3 Transaction behaviour

```
SpiWriteRead[p] ─▶ busy? ──yes──▶ SpiReply[p](SPI_OTHER_ERR)   (synchronous)
                     │no
                     ▼
            CS[p] LOW (sw) → DMA MISO (DATA → read) → DMA MOSI (write → DATA)
                     ▼ (ISR) dmaReplyIn[MISO], dmaReplyIn[MOSI] in either order
            both done → CS[p] HIGH → SpiReply[p](SPI_OK)         (ISR context)
```

The reply is emitted from the DMAC ISR. Reply handlers must only latch state; in
particular they must **not** queue a new SPI transaction from inside the callback
(see `Samd21.DmaDriver` SDD §3.4.2 and `Fram.FramDriver` for the main-context hop).

### 3.4 Error handling

**Receive overrun.** The SERCOM receive buffer is two characters deep. If the MISO DMA
channel loses arbitration for longer than two character times (observed on hardware at
12 MHz SCK with the UART and I2C channels on the same DMAC priority level), the SERCOM
drops a character and sets `STATUS.BUFOVF`/`INTFLAG.ERROR`. The MISO DMA block can then
never reach its count and, without intervention, the transaction never completes.
`configure()` therefore enables the SERCOM `ERROR` interrupt; `isrHandler()` aborts the
outstanding stream(s) through `dmaTransactionAbortOut` (the DMA driver clears the
channel's pending flags and raises no reply), acknowledges the overflow (bounded wait for
`TXC` so the character still in the shifter cannot land after the drain, write-1-clear
`BUFOVF`, drain `DATA`, clear `TXC`/`ERROR`), deasserts the software chip select and emits
`SpiReply` with `SPI_OTHER_ERR` and the caller's buffers. The SERCOM and DMAC interrupts share the lowest
NVIC priority, so neither handler preempts the other inside the driver. The reply is in
ISR context, like the success path. A busy reject (synchronous) and an overrun (ISR) both
use `SPI_OTHER_ERR`; `Drv.SpiStatus` has no finer value for a receive fault.

Nothing in the stack detects a transfer that produces no clock at all (e.g. SERCOM clock
gated); that failure mode has not been observed and a tick-based timeout is left for §6.

The DMA status is not currently folded into the reply (DMA bus errors surface as
`Samd21.DmaDriver` ISR replies with `BUS_ERROR`; the SPI reply remains `SPI_OK`) — see
§6. Programming errors (unconfigured, size mismatch, stray DMA reply, reconfigure) assert.

## 4. Integration

See `fprime-samd/Devices/Fram/Subtopology/Subtopology.fpp` for a complete example: two DMA
channels, a `Samd21.GpioDriver` chip select, and the `configComponents` phase.

## 5. Unit tests

`test/ut/` runs natively against the stub HAL: configuration in both chip-select modes,
reconfigure assert, request before configure, nominal transactions in both DMA completion
orders on every port, busy rejection on the same and other ports, hardware chip select,
size mismatch, stray/duplicate DMA replies, BAUD boundaries and overflow, 500
randomized transactions, and the receive-overrun path (both streams outstanding, one
stream outstanding, spurious interrupt, overrun while idle, recovery on every port and in
both chip-select modes) driven through the stub HAL's `fireIsr()`.

## 6. Future work

- Propagate DMA `BUS_ERROR` into `SpiReply` as `SPI_WRITE_ERR`/`SPI_OTHER_ERR`.
- A tick-based completion timeout for faults that raise no SERCOM flag (none observed).
- Raise the MISO channel above the MOSI/UART channels in DMAC priority so the overrun
  does not occur in the first place at high SCK rates.
- A main-context completion option (`activeIn`), mirroring `Samd21.I2cDriver`, so
  clients need not implement the hop themselves.
