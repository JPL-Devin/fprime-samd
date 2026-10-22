# Fram::FramDriver — Software Design Document

`Fram.FramDriver` is a passive F´ component that gives client components read and write access to an Infineon CY15B102QN 2 Mbit (256 KiB) serial F-RAM over SPI. A client calls the synchronous `read` or `write` port with a byte offset and an `Fw.Buffer`; the driver clocks the transfer through an asynchronous SPI host and delivers the outcome later, in main context, on the completion port of the same index. At startup it reads the device ID and the write-protect bits once and reports them in one event. It allocates nothing after construction, creates no thread or queue, and never asserts on what the hardware returns.

## 1. Requirements

| ID | Requirement | Verification |
|---|---|---|
| REQ-FRAM-001 | The FramDriver shall read `n` bytes at offset `o` into the request's buffer. | Unit test; integration test |
| REQ-FRAM-002 | The FramDriver shall write the request buffer's `n` bytes to the F-RAM at offset `o`. | Unit test; integration test |
| REQ-FRAM-003 | Every transaction shall comply with datasheet 002-19073 Rev. *O (opcode, address, data order, chip-select cycle). | Unit test (bytes vs datasheet); integration test |
| REQ-FRAM-004 | Accepted requests shall return `OK` after issuing the first transfer; completions carry offset, buffer, status. | Unit test |
| REQ-FRAM-005 | The FramDriver shall deliver completions on the index the request arrived on. | Unit test |
| REQ-FRAM-007 | The FramDriver shall reject sizes of 0 or above `MaxTransfer` with `INVALID_SIZE`, issuing nothing. | Unit test |
| REQ-FRAM-008 | The FramDriver shall reject `offset + size` beyond 262 144 bytes with `OUT_OF_RANGE`, issuing nothing. | Unit test |
| REQ-FRAM-009 | The FramDriver shall reject requests arriving during a transaction with `BUSY`, issuing nothing. | Unit test |
| REQ-FRAM-010 | Rejections shall be the request's return value; a rejected request receives no completion. | Unit test |
| REQ-FRAM-011 | SPI host failures shall complete the transaction `SPI_ERROR` with `FramSpiError`; the next request is accepted. | Unit test |
| REQ-FRAM-012 | The FramDriver shall `FW_ASSERT` on a request whose buffer data pointer is null. | Unit test |
| REQ-FRAM-013 | Spurious or mismatched SPI completions shall be ignored: no completion, no assert, state unchanged. | Unit test |
| REQ-FRAM-014 | At startup the FramDriver shall read the device ID once, logging `FramDeviceIdMismatch` on mismatch. | Unit test; integration test |
| REQ-FRAM-015 | A failed startup read shall log `FramDeviceIdReadFailed` or `FramStatusReadFailed` and not block the other. | Unit test |
| REQ-FRAM-016 | An ID mismatch or failed startup read shall not prevent servicing client requests. | Unit test |
| REQ-FRAM-017 | The FramDriver shall issue only READ, WRITE with WREN, RDSR and RDID transactions. | Unit test; integration test |
| REQ-FRAM-018 | The FramDriver shall issue no transaction before its first `schedIn` call. | Unit test |
| REQ-FRAM-019 | The FramDriver shall accept a request issued from within a completion callback. | Unit test |
| REQ-FRAM-021 | The FramDriver shall allocate nothing after construction; its static footprint shall not exceed 640 bytes. | Inspection (map file) |
| REQ-FRAM-022 | After reset or power cycle, ranges whose write completed `OK` shall read back unchanged. | Integration test |
| REQ-FRAM-023 | The FramDriver shall create no thread and no message queue. | Inspection |
| REQ-FRAM-024 | Requests arriving before startup transactions complete shall be rejected `BUSY`, issuing nothing. | Unit test |
| REQ-FRAM-025 | Rejection precedence shall be: null buffer (assert), `INVALID_SIZE`, `OUT_OF_RANGE`, `BUSY`. | Unit test |
| REQ-FRAM-026 | At startup the FramDriver shall read the status register once and report BP1/BP0 as `writeProtected`. | Unit test; integration test |
| REQ-FRAM-027 | The build shall fail for `MaxTransfer` of 0 or above `DEVICE_SIZE`, or `ClientPorts` of 0. | Inspection |
| REQ-FRAM-028 | When startup transactions complete, the FramDriver shall log `FramStartupComplete` (ID match, write protect) once. | Unit test; integration test |

## 2. Design
### 2.1 Block diagram
```
                                   ┌──────────────────────────────────────┐
              read[N] : Fram.Request ──▶ ■                              ■ ──▶ readComplete[N] : Fram.Reply
             write[N] : Fram.Request ──▶ ■        Fram.FramDriver       ■ ──▶ writeComplete[N] : Fram.Reply
spiReplyIn : Drv.SpiWriteReadCallback ──▶ ■  (passive; sync ports)      ■ ──▶ spiWriteReadOut : Drv.SpiWriteReadRequest
                 schedIn : Svc.Sched ──▶ ■                              │
                                   └──────────────────────────────────────┘
   N = Fram.Config.ClientPorts   special ports: eventOut, textEventOut, timeGetOut
```

### 2.2 Ports
| Name | Kind | Type | Purpose |
|---|---|---|---|
| `read[N]` | sync input | `Fram.Request` → `Fram.Status` | Read `buffer.size` bytes at `offset`; returns `OK` or the rejection (REQ-001, -010) |
| `write[N]` | sync input | `Fram.Request` → `Fram.Status` | Write `buffer.size` bytes at `offset`; returns `OK` or the rejection (REQ-002, -010) |
| `readComplete[N]` | output, `match … with read` | `Fram.Reply` | Completion (`OK`/`SPI_ERROR`) on the issuing index (REQ-005) |
| `writeComplete[N]` | output, `match … with write` | `Fram.Reply` | Completion (`OK`/`SPI_ERROR`) on the issuing index (REQ-005) |
| `spiWriteReadOut` | output | `Drv.SpiWriteReadRequest` | One full-duplex transfer = one chip-select cycle; every request is answered on `spiReplyIn` |
| `spiReplyIn` | sync input (main context, from the SPI host's `schedIn`) | `Drv.SpiWriteReadCallback` | Transfer outcome; drives the next phase or the completion (REQ-011, -013) |
| `schedIn` | sync input (main; required) | `Svc.Sched` | Issues the startup RDID / RDSR once (REQ-018, -024) |
SPI ports are the `Drv.AsyncGuardedSpi` client pair (nasa/fprime #6237, merged; `Samd21.SpiDriver` implements it); no commands, telemetry channels or parameters.
### 2.3 Events
| Event | Severity | Arguments | When |
|---|---|---|---|
| `FramSpiError` | WARNING_HI | `operation: Fram.Operation`, `spiStatus: Drv.SpiStatus`, `offset: U32`, `size: U32` | SPI host error on a client transfer; status `SPI_ERROR` (REQ-011) |
| `FramDeviceIdMismatch` | WARNING_HI | `expected: string 24`, `got: string 24` (18 hex chars) | Startup RDID ≠ `EXPECTED_DEVICE_ID` (REQ-014) |
| `FramDeviceIdReadFailed` | WARNING_LO | `spiStatus: Drv.SpiStatus` | Startup RDID transfer failed (REQ-015) |
| `FramStatusReadFailed` | WARNING_LO | `spiStatus: Drv.SpiStatus` | Startup RDSR transfer failed (REQ-015) |
| `FramStartupComplete` | ACTIVITY_LO | `deviceIdMatch: bool` (false if RDID failed), `writeProtected: bool` (BP1 or BP0 set), `statusRegister: U8` (0xFF if RDSR failed) | Both startup transactions done; requests accepted (REQ-026, -028) |

### 2.4 Configure
`Fram.Config` (module `FramConfig`, project-overridable) sizes the driver: one request/reply index pair per client, and the largest single transfer, which sets the two static SPI staging buffers at `2 × (MaxTransfer + 4)` bytes — about 530 B of the 640 B footprint budget at the defaults; a two-segment SPI transfer (SPI-driver PR) removes the staging (REQ-021, -027).
```fpp
module Fram { module Config {
    constant ClientPorts = 2      # client request/reply index pairs (> 0)
    constant MaxTransfer = 128    # bytes per request, 1 ... DEVICE_SIZE
} }
```
`FramDeviceIdConfig.hpp` (same module) holds the RDID the startup check expects, in MISO byte order; the 3.0 V CY15V102QN ends `... 04` (REQ-014).
```cpp
constexpr U8 EXPECTED_DEVICE_ID[9] = {0x00, 0x2A, 0xC2, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F};  // CY15B102QN
```
`Fram.Subtopology` instantiates `framDriver` and its `spiDriver`; `FramSubtopologyConfig` (overridable) sets the ids and the SPI host (4 MHz, mode 0, MSB first on SERCOM1 PA16/PA17/PA19). Device geometry (`Fram.DEVICE_SIZE` 0x40000, 3 address bytes, 9-byte ID, BP mask 0x0C) is fixed, not configured.
```fpp
module Fram { module SubtopologyConfig {
    constant BASE_ID  = 0xDD00    # framDriver events at BASE_ID + 0 ... 4; spiDriver at + 0x100
    constant SPI_SLOT = 0         # spiDriver chip-select slot the F-RAM hangs off
} }
```
```cpp
constexpr Samd21::SercomKind::T SERCOM = Samd21::SercomKind::SERCOM_1;
constexpr U32 BAUD_RATE_KHZ = 4000;   // with CLOCK_POLARITY IdleLow, CLOCK_PHASE SampleOnLeadingSck (mode 0)
```
The deployment schedules both drivers through the passive-cycler → `Svc.Sched` adapter (fprime-samd PR), SPI host first so a completion and the phase it triggers land in one cycle, and connects each client pair unindexed — `match` keeps request and completion on one index.
```fpp
cycler.cycleOut -> schedAdapter.activeIn            # adapter returns false: one pass per cycle
schedAdapter.schedOut[0] -> Fram.spiDriver.schedIn  # SpiDriver leaves ISR context here
schedAdapter.schedOut[1] -> Fram.framDriver.schedIn
client.framReadOut -> Fram.framDriver.read          Fram.framDriver.readComplete -> client.readCompleteIn
```
### 2.5 Behavior
On its first `schedIn` the driver issues RDID (9Fh), on that reply issues RDSR (05h), and on that reply logs `FramStartupComplete(deviceIdMatch, writeProtected, statusRegister)` once — preceded by `FramDeviceIdMismatch`, `FramDeviceIdReadFailed` or `FramStatusReadFailed` when applicable — and starts accepting requests; until then every request returns `BUSY`. A request is checked in the order null buffer (assert, caller fault), `INVALID_SIZE` (0 or above `MaxTransfer`), `OUT_OF_RANGE` (offset + size above `DEVICE_SIZE`), `BUSY` (transaction in flight); a rejected request gets no completion, an accepted one records index, offset and buffer, issues READ 03h (or WREN 06h for a write) as one chip-select cycle through `spiWriteReadOut`, and returns `OK`. Every `spiReplyIn` — completion or refusal (`SPI_BUSY_ERR`, `SPI_CONFIG_ERR`) — arrives in main context from the SPI host's `schedIn`, never re-entrantly, so the driver acts on it directly: after WREN it issues WRITE 02h, after READ it copies the received bytes into the client buffer, after any failed transfer it logs `FramSpiError` and substitutes `SPI_ERROR`, then returns to `IDLE` and invokes `readComplete[i]` / `writeComplete[i]` with offset, buffer and status on the issuing index. A reply that does not name the in-flight staging buffers, or arrives while idle, is ignored — nothing completes, nothing asserts, the next request is served. Because the state is `IDLE` before the completion call, a client may issue its next request from inside the callback; with no ISR path left in the component, no atomics or critical sections are needed.
```mermaid
sequenceDiagram
    participant S as SpiDriver (Drv.AsyncGuardedSpi; DMAC ISR latches, schedIn replies)
    participant Y as schedAdapter (main)
    Client->>FramDriver: write[i](offset, buffer)
    FramDriver->>S: spiWriteReadOut(WREN)   [WREN_IN_FLIGHT]
    FramDriver-->>Client: return OK
    Y->>S: schedIn   (DMA done, latched in the ISR)
    S->>FramDriver: spiReplyIn(SPI_OK)
    FramDriver->>S: spiWriteReadOut(WRITE 02h A2 A1 A0 data)   [WRITE_IN_FLIGHT]
    Y->>S: schedIn
    S->>FramDriver: spiReplyIn(status)   [→ IDLE]
    FramDriver->>Client: writeComplete[i](offset, buffer, OK | SPI_ERROR)
    Client->>FramDriver: read[i](…) from the callback → OK (REQ-019)
```

## 3. Tests

### 3.1 Unit tests
| Test | Description | Requirements |
|---|---|---|
| UT-01 | read at 0x012345 delivers 7 bytes; TX `03 01 23 45`; `readComplete[0]` OK | REQ-FRAM-001 |
| UT-02 | write at 0x3FF80: WREN then `02 03 FF 80` + 128 bytes; `writeComplete[0]` OK | REQ-FRAM-002 |
| UT-03 | opcode + 3 MSB-first address bytes; read is one transfer; write is WREN then WRITE | REQ-FRAM-003 |
| UT-04 | request returns OK with one transfer issued; completion only after the reply | REQ-FRAM-004 |
| UT-05 | completion index equals request index for read on 1 and write on 0 | REQ-FRAM-005 |
| UT-07 | size 0 and 129 return INVALID_SIZE, nothing issued; size 128 accepted | REQ-FRAM-007 |
| UT-08 | offset + size > DEVICE_SIZE returns OUT_OF_RANGE; == DEVICE_SIZE accepted with address `03 FF 80` | REQ-FRAM-008 |
| UT-09 | second and third requests return BUSY; only the first completes with its own offset/buffer | REQ-FRAM-009 |
| UT-10 | rejected requests never produce a completion over 5 further `schedIn` calls | REQ-FRAM-010 |
| UT-11 | reply errors (incl. SPI_BUSY_ERR refusal) on READ/WREN/WRITE → `FramSpiError`, SPI_ERROR; next request OK | REQ-FRAM-011 |
| UT-12 | null buffer on read and on write fires the assert hook; nothing issued | REQ-FRAM-012 |
| UT-13 | reply while idle, with foreign buffers, or duplicated: no completion, no assert, next request OK | REQ-FRAM-013 |
| UT-14 | one RDID (`9F`+9 bytes) per boot; last byte +1 → `FramDeviceIdMismatch(expected, got)` | REQ-FRAM-014 |
| UT-15 | RDID error → `FramDeviceIdReadFailed`, RDSR still issued; RDSR error → `FramStatusReadFailed` | REQ-FRAM-015 |
| UT-16 | after mismatch or failed startup reads a client read completes OK | REQ-FRAM-016 |
| UT-17 | every captured TX opcode ∈ {03, 02, 06, 05, 9F} | REQ-FRAM-017 |
| UT-18 | request before the first `schedIn` returns BUSY; no transfer until the first `schedIn` | REQ-FRAM-018 |
| UT-19 | write issued from inside `readComplete` is accepted; original completion arguments intact | REQ-FRAM-019 |
| UT-21 | `static_assert(sizeof(FramDriver) + file-scope objects <= 640)`; failing `operator new` links | REQ-FRAM-021 |
| UT-24 | reads before the RDID and RDSR replies return BUSY; accepted after startup | REQ-FRAM-024 |
| UT-25 | null + size 0 asserts; size 0 out-of-range → INVALID_SIZE; busy + out-of-range → OUT_OF_RANGE | REQ-FRAM-025 |
| UT-26 | RDSR 0x0C → `FramStartupComplete(true, true, 0x0C)`, write still completes OK; 0x40 → unprotected | REQ-FRAM-026 |
| UT-27 | configure fails for `MaxTransfer` 0, `MaxTransfer` > DEVICE_SIZE, `ClientPorts` 0 (negative builds) | REQ-FRAM-027 |
| UT-28 | once per boot; failed RDID → `(false, false, 0x00)`, failed RDSR → `(true, false, 0xFF)` | REQ-FRAM-028 |
| UT-RB | STest rules Read/Write/RejectSize/RejectRange/Busy/SpiError/SpuriousReply/Callback, 10 000 random steps, invariants hold | REQ-FRAM-001, -002, -004, -005, -007, -008, -009, -010, -011, -013, -019, -025 |
| INS-23 | source inspection: FramDriver creates no thread, queue or OS object | REQ-FRAM-023 |

### 3.2 Integration tests (any deployment with the component)
| Test | Description | Requirements |
|---|---|---|
| INT-1 | after SWD reset: `FramStartupComplete(true, false, …)`, no warnings, two boot segments | REQ-FRAM-014, -017, -018, -026, -028 |
| INT-2 | 128-byte write and read-back at offset 0 and the last page, index 0 | REQ-FRAM-001, -002, -003, -004, -005, -008 |
| INT-3 | single bytes at 0x000001, 0x000100, 0x010000 read back without aliasing | REQ-FRAM-003 |
| INT-4 | all 2048 pages written then verified with chained completions, all in thread mode | REQ-FRAM-001, -002, -004, -005, -019 |
| INT-5 | write/verify on index 1 and verify on index 0 complete on their own indices | REQ-FRAM-005 |
| INT-6 | full-device image survives an MCU reset (startup event, RCAUSE) and verifies | REQ-FRAM-022, -014, -017, -028 |
| INT-7 | the same image survives a hub-port power cycle and verifies | REQ-FRAM-022 |
| INT-8 | 300 s verify-then-write soak: ≥ 280 pairs, zero stalls, mismatches, SPI errors | REQ-FRAM-019, -011 |
| INT-9 | SWD reset during a continuous sweep: clean startup, every page reads back | REQ-FRAM-014, -018, -022, -028 |
| INT-10 | second request during a transaction returns BUSY, writes nothing, first completes | REQ-FRAM-009, -010, -004 |
| INT-11 | requests past the device end return OUT_OF_RANGE; last page unchanged | REQ-FRAM-008, -010 |
| INT-12 | sizes 0 and 129 → INVALID_SIZE despite out-of-range or busy; busy + out-of-range → OUT_OF_RANGE | REQ-FRAM-007, -010, -025 |
| INT-13 | device left as the handed-over pseudo-random image; independent dump equals it byte-for-byte | REQ-FRAM-002, -003, -022 |
