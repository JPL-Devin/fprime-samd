// ======================================================================
// \title  FramDriverTester.cpp
// \brief  Unit tests for Fram::FramDriver (rows UT-xx of docs/sdd.md §3.1)
// ======================================================================

#include "FramDriverTester.hpp"

#include <cstring>
#include <string>

#include "STest/Pick/Pick.hpp"
#include "fprime-samd/Devices/Fram/FramConfig/FramDeviceIdConfig.hpp"

namespace Fram {

static const U8 WREN = 0x06, WRITE = 0x02, READ = 0x03, RDSR = 0x05, RDID = 0x9F;

FramDriverTester ::FramDriverTester()
    : FramDriverGTestBase("FramDriverTester", MAX_HISTORY_SIZE),
      component("FramDriver"),
      m_lastWrite(),
      m_lastRead(),
      m_spiCount(0),
      m_data(),
      m_buffer(),
      m_readPorts(),
      m_writePorts(),
      m_opcodes(),
      m_requestFromCallback(false),
      m_callbackVerdict(Fram::Status::OK) {
    this->initComponents();
    this->connectPorts();
    for (FwIndexType p = 0; p < N; p++) {
        for (U32 i = 0; i < MAX; i++) {
            this->m_data[p][i] = static_cast<U8>(0x11 * (p + 1) + i);
        }
    }
}

FramDriverTester ::~FramDriverTester() {}

// ----------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------

void FramDriverTester ::testRead() {
    // UT-01: read at 0x012345 delivers 7 bytes; TX 03 01 23 45; readComplete[0] OK
    this->startup();
    U8 device[7] = {1, 2, 3, 4, 5, 6, 7};
    ASSERT_EQ(this->read(0, 0x012345, 7), Fram::Status::OK);
    this->assertHeader(READ, 0x012345, HDR + 7);
    for (U32 i = HDR; i < HDR + 7; i++) {
        ASSERT_EQ(this->lastTx()[i], 0) << "read data phase clocks zeros";
    }
    this->finishRead(0, 0x012345, 7, device);
    ASSERT_EQ(std::memcmp(this->m_data[0], device, 7), 0);
}

void FramDriverTester ::testWrite() {
    // UT-02: write at 0x3FF80: WREN then 02 03 FF 80 + 128 bytes; writeComplete[0] OK
    // UT-03: opcode + 3 MSB-first address bytes; read is one transfer; write is WREN then WRITE
    this->startup();
    ASSERT_EQ(this->write(0, 0x3FF80, MAX), Fram::Status::OK);
    ASSERT_EQ(this->m_spiCount, 3u);  // RDID, RDSR, WREN
    ASSERT_EQ(this->lastLen(), 1u);
    ASSERT_EQ(this->lastTx()[0], WREN);
    this->reply(Drv::SpiStatus::SPI_OK);
    ASSERT_EQ(this->m_spiCount, 4u);
    this->assertHeader(WRITE, 0x3FF80, HDR + MAX);
    ASSERT_EQ(std::memcmp(&this->lastTx()[HDR], this->m_data[0], MAX), 0);
    ASSERT_from_writeComplete_SIZE(0);
    this->reply(Drv::SpiStatus::SPI_OK);
    ASSERT_from_writeComplete_SIZE(1);
    ASSERT_from_writeComplete(0, 0x3FF80u, this->m_buffer[0], Fram::Status(Fram::Status::OK));
    // address bytes for 1 and 0x010000 (UT-03)
    ASSERT_EQ(this->read(0, 1, 1), Fram::Status::OK);
    this->assertHeader(READ, 1, HDR + 1);
    U8 one = 0;
    this->finishRead(0, 1, 1, &one);
    ASSERT_EQ(this->read(0, 0x010000, 1), Fram::Status::OK);
    this->assertHeader(READ, 0x010000, HDR + 1);
    this->finishRead(0, 0x010000, 1, &one);
}

void FramDriverTester ::testAcceptThenComplete() {
    // UT-04: request returns OK with one transfer issued; completion only after the reply
    // UT-10: rejected requests never produce a completion over further schedIn calls
    this->startup();
    const U32 before = this->m_spiCount;
    ASSERT_EQ(this->read(1, 0x100, 4), Fram::Status::OK);
    ASSERT_EQ(this->m_spiCount, before + 1);
    for (U32 i = 0; i < 5; i++) {
        this->invoke_to_schedIn(0, i);
    }
    ASSERT_from_readComplete_SIZE(0);
    ASSERT_EQ(this->read(0, 0, 4), Fram::Status::BUSY);
    ASSERT_EQ(this->write(0, DEV, 4), Fram::Status::OUT_OF_RANGE);
    ASSERT_EQ(this->m_spiCount, before + 1);
    U8 d[4] = {9, 9, 9, 9};
    this->finishRead(1, 0x100, 4, d);
    for (U32 i = 0; i < 5; i++) {
        this->invoke_to_schedIn(0, i);
    }
    ASSERT_from_readComplete_SIZE(1);
    ASSERT_from_writeComplete_SIZE(0);
    ASSERT_EQ(this->m_spiCount, before + 1);
}

void FramDriverTester ::testIndexRouting() {
    // UT-05: completion index equals request index for read on 1 and write on 0
    this->startup();
    U8 d[2] = {5, 6};
    ASSERT_EQ(this->read(1, 0x20, 2), Fram::Status::OK);
    this->finishRead(1, 0x20, 2, d);
    ASSERT_EQ(this->m_readPorts[0], 1);
    ASSERT_EQ(this->write(0, 0x40, 2), Fram::Status::OK);
    this->finishWrite(0, 0x40, 2);
    ASSERT_EQ(this->m_writePorts[0], 0);
    ASSERT_from_readComplete_SIZE(1);
    ASSERT_from_writeComplete_SIZE(1);
}

void FramDriverTester ::testInvalidSize() {
    // UT-07: size 0 and 129 return INVALID_SIZE, nothing issued; size 128 accepted
    this->startup();
    const U32 before = this->m_spiCount;
    U8 big[MAX + 1] = {};
    Fw::Buffer zero(big, 0);
    Fw::Buffer over(big, MAX + 1);
    ASSERT_EQ(this->invoke_to_read(0, 0, zero), Fram::Status::INVALID_SIZE);
    ASSERT_EQ(this->invoke_to_write(0, 0, zero), Fram::Status::INVALID_SIZE);
    ASSERT_EQ(this->invoke_to_read(0, 0, over), Fram::Status::INVALID_SIZE);
    ASSERT_EQ(this->invoke_to_write(0, 0, over), Fram::Status::INVALID_SIZE);
    ASSERT_EQ(this->m_spiCount, before);
    ASSERT_EQ(this->read(0, 0, MAX), Fram::Status::OK);
    ASSERT_EQ(this->m_spiCount, before + 1);
}

void FramDriverTester ::testOutOfRange() {
    // UT-08: offset + size > DEVICE_SIZE returns OUT_OF_RANGE; == DEVICE_SIZE accepted with address 03 FF 80
    this->startup();
    const U32 before = this->m_spiCount;
    ASSERT_EQ(this->read(0, DEV - MAX + 1, MAX), Fram::Status::OUT_OF_RANGE);
    ASSERT_EQ(this->write(0, DEV, 1), Fram::Status::OUT_OF_RANGE);
    ASSERT_EQ(this->read(0, 0xFFFFFFFFu, 1), Fram::Status::OUT_OF_RANGE);  // no wrap-around
    ASSERT_EQ(this->m_spiCount, before);
    ASSERT_EQ(this->read(0, DEV - MAX, MAX), Fram::Status::OK);
    this->assertHeader(READ, 0x3FF80, HDR + MAX);
}

void FramDriverTester ::testBusy() {
    // UT-09: second and third requests return BUSY; only the first completes with its own offset/buffer
    this->startup();
    ASSERT_EQ(this->write(0, 0x10, 3), Fram::Status::OK);
    const U32 count = this->m_spiCount;
    ASSERT_EQ(this->read(1, 0x20, 3), Fram::Status::BUSY);
    ASSERT_EQ(this->write(1, 0x30, 3), Fram::Status::BUSY);
    ASSERT_EQ(this->m_spiCount, count);
    this->finishWrite(0, 0x10, 3);
    ASSERT_from_writeComplete_SIZE(1);
    ASSERT_from_readComplete_SIZE(0);
    ASSERT_EQ(this->m_writePorts[0], 0);
}

void FramDriverTester ::testSpiErrors() {
    // UT-11: reply errors (incl. SPI_BUSY_ERR refusal) on READ/WREN/WRITE -> FramSpiError, SPI_ERROR; next request OK
    this->startup();
    // READ fails
    ASSERT_EQ(this->read(0, 0x100, 4), Fram::Status::OK);
    this->reply(Drv::SpiStatus::SPI_WRITE_ERR);
    ASSERT_EVENTS_FramSpiError_SIZE(1);
    ASSERT_EVENTS_FramSpiError(0, Fram::Operation(Fram::Operation::READ), Drv::SpiStatus(Drv::SpiStatus::SPI_WRITE_ERR),
                               0x100u, 4u);
    ASSERT_from_readComplete_SIZE(1);
    ASSERT_from_readComplete(0, 0x100u, this->m_buffer[0], Fram::Status(Fram::Status::SPI_ERROR));
    // WREN refused by the host: no WRITE issued
    this->clearHistory();
    ASSERT_EQ(this->write(1, 0x200, 8), Fram::Status::OK);
    const U32 count = this->m_spiCount;
    this->reply(Drv::SpiStatus::SPI_BUSY_ERR);
    ASSERT_EQ(this->m_spiCount, count);
    ASSERT_EVENTS_FramSpiError(0, Fram::Operation(Fram::Operation::WRITE_ENABLE),
                               Drv::SpiStatus(Drv::SpiStatus::SPI_BUSY_ERR), 0x200u, 8u);
    ASSERT_from_writeComplete_SIZE(1);
    ASSERT_EQ(this->m_writePorts[0], 1);
    ASSERT_from_writeComplete(0, 0x200u, this->m_buffer[1], Fram::Status(Fram::Status::SPI_ERROR));
    // WRITE fails after a good WREN
    this->clearHistory();
    ASSERT_EQ(this->write(0, 0x300, 2), Fram::Status::OK);
    this->reply(Drv::SpiStatus::SPI_OK);
    this->assertHeader(WRITE, 0x300, HDR + 2);
    this->reply(Drv::SpiStatus::SPI_OTHER_ERR);
    ASSERT_EVENTS_FramSpiError(0, Fram::Operation(Fram::Operation::WRITE),
                               Drv::SpiStatus(Drv::SpiStatus::SPI_OTHER_ERR), 0x300u, 2u);
    ASSERT_from_writeComplete(0, 0x300u, this->m_buffer[0], Fram::Status(Fram::Status::SPI_ERROR));
    // next request is served
    this->clearHistory();
    U8 d[4] = {};
    ASSERT_EQ(this->read(0, 0, 4), Fram::Status::OK);
    this->finishRead(0, 0, 4, d);
}

void FramDriverTester ::testNullBuffer() {
    // UT-12: null buffer on read and on write fires the assert hook; nothing issued
    this->startup();
    Fw::Buffer null(nullptr, 4);
    const U32 before = this->m_spiCount;
    ASSERT_DEATH_IF_SUPPORTED(this->invoke_to_read(0, 0, null), "");
    ASSERT_DEATH_IF_SUPPORTED(this->invoke_to_write(0, 0, null), "");
    ASSERT_EQ(this->m_spiCount, before);
}

void FramDriverTester ::testSpuriousReplies() {
    // UT-13: reply while idle, with foreign buffers, or duplicated: no completion, no assert, next request OK
    this->startup();
    U8 fw[8] = {}, fr[8] = {};
    Fw::Buffer foreignW(fw, 8), foreignR(fr, 8);
    // idle
    this->invoke_to_spiReplyIn(0, this->m_lastWrite, this->m_lastRead, Drv::SpiStatus::SPI_OK);
    this->invoke_to_spiReplyIn(0, foreignW, foreignR, Drv::SpiStatus::SPI_OK);
    // in flight, foreign buffers
    ASSERT_EQ(this->read(0, 0x10, 4), Fram::Status::OK);
    this->invoke_to_spiReplyIn(0, foreignW, foreignR, Drv::SpiStatus::SPI_OK);
    this->invoke_to_spiReplyIn(0, foreignW, this->m_lastRead, Drv::SpiStatus::SPI_OK);
    this->invoke_to_spiReplyIn(0, this->m_lastWrite, foreignR, Drv::SpiStatus::SPI_OK);
    ASSERT_from_readComplete_SIZE(0);
    ASSERT_EQ(this->read(1, 0, 1), Fram::Status::BUSY);
    // the real reply, then a duplicate of it
    U8 d[4] = {1, 2, 3, 4};
    this->finishRead(0, 0x10, 4, d);
    Fw::Buffer w = this->m_lastWrite, r = this->m_lastRead;
    this->invoke_to_spiReplyIn(0, w, r, Drv::SpiStatus::SPI_OK);
    this->invoke_to_spiReplyIn(0, w, r, Drv::SpiStatus::SPI_OTHER_ERR);
    ASSERT_from_readComplete_SIZE(1);
    ASSERT_EVENTS_SIZE(1);  // only FramStartupComplete
    ASSERT_EQ(this->write(1, 0, 1), Fram::Status::OK);
    this->finishWrite(1, 0, 1);
}

void FramDriverTester ::testDeviceId() {
    // UT-14: one RDID (9F + 9 bytes) per boot; last byte +1 -> FramDeviceIdMismatch(expected, got)
    // UT-16: after a mismatch a client read completes OK
    U8 wrong[Fram::DEVICE_ID_SIZE];
    std::memcpy(wrong, Fram::EXPECTED_DEVICE_ID, sizeof(wrong));
    wrong[Fram::DEVICE_ID_SIZE - 1] = static_cast<U8>(wrong[Fram::DEVICE_ID_SIZE - 1] + 1);
    this->startup(Drv::SpiStatus::SPI_OK, wrong);
    ASSERT_EVENTS_FramDeviceIdMismatch_SIZE(1);
    // Event strings are truncated to the deployment's FW_LOG_STRING_MAX_SIZE (2 in the reference
    // configuration, so only the first byte is visible there; >= 18 shows the whole ID)
    const std::string expected = std::string("002AC27F7F7F7F7F7F").substr(0, FW_LOG_STRING_MAX_SIZE);
    const std::string got = std::string("002AC27F7F7F7F7F80").substr(0, FW_LOG_STRING_MAX_SIZE);
    ASSERT_EVENTS_FramDeviceIdMismatch(0, expected.c_str(), got.c_str());
    ASSERT_EVENTS_FramStartupComplete(0, false, false, 0x00);
    U32 rdid = 0;
    // exactly one RDID so far, and none after further schedIn calls
    for (U32 i = 0; i < 3; i++) {
        this->invoke_to_schedIn(0, i);
    }
    for (U32 i = 0; i < this->m_spiCount && i < MAX_HISTORY_SIZE; i++) {
        if (this->m_opcodes[i] == RDID) {
            rdid++;
        }
    }
    ASSERT_EQ(rdid, 1u);
    U8 d[2] = {};
    ASSERT_EQ(this->read(0, 0, 2), Fram::Status::OK);
    this->finishRead(0, 0, 2, d);
    // nominal id: no mismatch event
    FramDriverTester ok;
    ok.startup();
    ASSERT_EQ(ok.eventHistory_FramDeviceIdMismatch->size(), 0u);
}

void FramDriverTester ::testStartupReadFailures() {
    // UT-15: RDID error -> FramDeviceIdReadFailed, RDSR still issued; RDSR error -> FramStatusReadFailed
    // UT-28: FramStartupComplete once per boot; failed RDID -> (false,false,0x00); failed RDSR -> (true,false,0xFF)
    this->startup(Drv::SpiStatus::SPI_OTHER_ERR);
    ASSERT_EVENTS_FramDeviceIdReadFailed_SIZE(1);
    ASSERT_EVENTS_FramDeviceIdReadFailed(0, Drv::SpiStatus(Drv::SpiStatus::SPI_OTHER_ERR));
    ASSERT_EQ(this->lastTx()[0], RDSR);
    ASSERT_EVENTS_FramStartupComplete_SIZE(1);
    ASSERT_EVENTS_FramStartupComplete(0, false, false, 0x00);
    ASSERT_EQ(this->eventHistory_FramDeviceIdMismatch->size(), 0u);
    U8 d[1] = {};
    ASSERT_EQ(this->read(0, 0, 1), Fram::Status::OK);  // UT-16 after a failed read
    this->finishRead(0, 0, 1, d);

    FramDriverTester sr;
    sr.startup(Drv::SpiStatus::SPI_OK, Fram::EXPECTED_DEVICE_ID, Drv::SpiStatus::SPI_WRITE_ERR);
    ASSERT_EQ(sr.eventHistory_FramStatusReadFailed->size(), 1u);
    ASSERT_EQ(sr.eventHistory_FramStartupComplete->size(), 1u);
    ASSERT_EQ(sr.eventHistory_FramStartupComplete->at(0).deviceIdMatch, true);
    ASSERT_EQ(sr.eventHistory_FramStartupComplete->at(0).writeProtected, false);
    ASSERT_EQ(sr.eventHistory_FramStartupComplete->at(0).statusRegister, 0xFF);
    for (U32 i = 0; i < 3; i++) {
        sr.invoke_to_schedIn(0, i);
    }
    ASSERT_EQ(sr.eventHistory_FramStartupComplete->size(), 1u);
}

void FramDriverTester ::testOpcodes() {
    // UT-17: every captured TX opcode in {03, 02, 06, 05, 9F}
    this->startup();
    U8 d[3] = {};
    ASSERT_EQ(this->read(0, 5, 3), Fram::Status::OK);
    this->finishRead(0, 5, 3, d);
    ASSERT_EQ(this->write(1, 7, 3), Fram::Status::OK);
    this->finishWrite(1, 7, 3);
    ASSERT_EQ(this->fromPortHistory_spiWriteReadOut->size(), 5u);
    for (U32 i = 0; i < this->m_spiCount; i++) {
        const U8 op = this->m_opcodes[i];
        ASSERT_TRUE(op == READ || op == WRITE || op == WREN || op == RDSR || op == RDID) << "opcode " << op;
    }
}

void FramDriverTester ::testBeforeFirstSched() {
    // UT-18: request before the first schedIn returns BUSY; no transfer until the first schedIn
    // UT-24: reads before the RDID and RDSR replies return BUSY; accepted after startup
    ASSERT_EQ(this->read(0, 0, 1), Fram::Status::BUSY);
    ASSERT_EQ(this->write(1, 0, 1), Fram::Status::BUSY);
    ASSERT_EQ(this->m_spiCount, 0u);
    this->invoke_to_schedIn(0, 0);
    ASSERT_EQ(this->m_spiCount, 1u);
    ASSERT_EQ(this->lastTx()[0], RDID);
    ASSERT_EQ(this->read(0, 0, 1), Fram::Status::BUSY);
    U8 id[1 + Fram::DEVICE_ID_SIZE] = {0};
    std::memcpy(&id[1], Fram::EXPECTED_DEVICE_ID, Fram::DEVICE_ID_SIZE);
    this->reply(Drv::SpiStatus::SPI_OK, id, sizeof(id));
    ASSERT_EQ(this->lastTx()[0], RDSR);
    ASSERT_EQ(this->read(0, 0, 1), Fram::Status::BUSY);
    U8 sr[2] = {0, 0};
    this->reply(Drv::SpiStatus::SPI_OK, sr, 2);
    ASSERT_EVENTS_FramStartupComplete(0, true, false, 0x00);
    ASSERT_from_readComplete_SIZE(0);
    ASSERT_EQ(this->read(0, 0, 1), Fram::Status::OK);
}

void FramDriverTester ::testRequestFromCallback() {
    // UT-19: write issued from inside readComplete is accepted; original completion arguments intact
    this->startup();
    this->m_requestFromCallback = true;
    U8 d[4] = {4, 3, 2, 1};
    ASSERT_EQ(this->read(0, 0x50, 4), Fram::Status::OK);
    this->finishRead(0, 0x50, 4, d);
    ASSERT_EQ(this->m_callbackVerdict, Fram::Status::OK);
    ASSERT_from_readComplete(0, 0x50u, this->m_buffer[0], Fram::Status(Fram::Status::OK));
    // the callback's write is now in flight: WREN was issued
    ASSERT_EQ(this->lastTx()[0], WREN);
    this->m_requestFromCallback = false;
    this->finishWrite(1, 0x60, 4);
}

void FramDriverTester ::testFootprint() {
    // UT-21: no dynamic allocation after construction (link-time: see FramDriverTestMain) and a
    // bounded object. The 640 B budget is for the Cortex-M0+ map file (4-byte pointers); the
    // native 64-bit object may be up to twice as large.
    static_assert(sizeof(FramDriver) <= 2 * 640, "FramDriver static footprint");
    ASSERT_LE(sizeof(FramDriver), static_cast<size_t>(2 * 640));
}

void FramDriverTester ::testPrecedence() {
    // UT-25: null + size 0 asserts; size 0 out-of-range -> INVALID_SIZE; busy + out-of-range -> OUT_OF_RANGE
    this->startup();
    Fw::Buffer nullZero(nullptr, 0);
    ASSERT_DEATH_IF_SUPPORTED(this->invoke_to_read(0, 0, nullZero), "");
    U8 d[MAX + 1] = {};
    Fw::Buffer zero(d, 0), over(d, MAX + 1);
    ASSERT_EQ(this->invoke_to_read(0, DEV, zero), Fram::Status::INVALID_SIZE);
    ASSERT_EQ(this->read(0, 0, 4), Fram::Status::OK);  // now busy
    ASSERT_EQ(this->invoke_to_read(1, DEV, over), Fram::Status::INVALID_SIZE);
    ASSERT_EQ(this->read(1, DEV, 1), Fram::Status::OUT_OF_RANGE);
    ASSERT_EQ(this->read(1, 0, 1), Fram::Status::BUSY);
}

void FramDriverTester ::testWriteProtect() {
    // UT-26: RDSR 0x0C -> FramStartupComplete(true, true, 0x0C), write still completes OK; 0x40 -> unprotected
    this->startup(Drv::SpiStatus::SPI_OK, Fram::EXPECTED_DEVICE_ID, Drv::SpiStatus::SPI_OK, 0x0C);
    ASSERT_EVENTS_FramStartupComplete(0, true, true, 0x0C);
    ASSERT_EQ(this->write(0, 0, 4), Fram::Status::OK);
    this->finishWrite(0, 0, 4);
    FramDriverTester clear;
    clear.startup(Drv::SpiStatus::SPI_OK, Fram::EXPECTED_DEVICE_ID, Drv::SpiStatus::SPI_OK, 0x40);
    ASSERT_EQ(clear.eventHistory_FramStartupComplete->at(0).writeProtected, false);
    ASSERT_EQ(clear.eventHistory_FramStartupComplete->at(0).statusRegister, 0x40);
}

void FramDriverTester ::testRandomScenario() {
    // UT-RB: random reads/writes/rejections/spurious replies/SPI errors against a model of the
    // one-transaction-at-a-time contract; invariants checked at every step
    this->startup();
    U8 shadow[MAX] = {};
    U32 completions = 0, accepted = 0;
    for (U32 step = 0; step < 10000; step++) {
        const FwIndexType port = static_cast<FwIndexType>(STest::Pick::lowerUpper(0, N - 1));
        const U32 size = STest::Pick::lowerUpper(0, MAX + 1);
        const U32 offset = STest::Pick::lowerUpper(0, 1) ? STest::Pick::lowerUpper(0, DEV - 1) : DEV - size;
        const bool isWrite = STest::Pick::lowerUpper(0, 1) == 1;
        const U32 spiBefore = this->m_spiCount;
        Fw::Buffer buf(this->m_data[port], size);
        const Fram::Status verdict =
            isWrite ? this->invoke_to_write(port, offset, buf) : this->invoke_to_read(port, offset, buf);
        Fram::Status expected = Fram::Status::OK;
        if (size == 0 || size > MAX) {
            expected = Fram::Status::INVALID_SIZE;
        } else if (offset > DEV - size) {
            expected = Fram::Status::OUT_OF_RANGE;
        }
        ASSERT_EQ(verdict, expected) << "step " << step;
        if (verdict != Fram::Status::OK) {
            ASSERT_EQ(this->m_spiCount, spiBefore);
            continue;
        }
        accepted++;
        ASSERT_EQ(this->m_spiCount, spiBefore + 1);
        // while in flight everything is BUSY and spurious replies change nothing
        ASSERT_EQ(this->read(static_cast<FwIndexType>((port + 1) % N), 0, 1), Fram::Status::BUSY);
        U8 fw[4] = {}, fr[4] = {};
        Fw::Buffer foreignW(fw, 4), foreignR(fr, 4);
        this->invoke_to_spiReplyIn(0, foreignW, foreignR, Drv::SpiStatus::SPI_OK);
        ASSERT_EQ(this->fromPortHistory_readComplete->size() + this->fromPortHistory_writeComplete->size(),
                  completions);
        const bool fail = STest::Pick::lowerUpper(0, 9) == 0;
        if (isWrite) {
            ASSERT_EQ(this->lastTx()[0], WREN);
            if (fail && STest::Pick::lowerUpper(0, 1)) {
                this->reply(Drv::SpiStatus::SPI_BUSY_ERR);
            } else {
                this->reply(Drv::SpiStatus::SPI_OK);
                this->assertHeader(WRITE, offset, HDR + size);
                this->reply(fail ? Drv::SpiStatus::SPI_OTHER_ERR : Drv::SpiStatus::SPI_OK);
            }
            ASSERT_EQ(this->fromPortHistory_writeComplete->size(),
                      0u + (completions + 1 - this->fromPortHistory_readComplete->size()));
            ASSERT_EQ(this->m_writePorts[this->fromPortHistory_writeComplete->size() - 1], port);
        } else {
            this->assertHeader(READ, offset, HDR + size);
            if (fail) {
                this->reply(Drv::SpiStatus::SPI_WRITE_ERR);
            } else {
                this->reply(Drv::SpiStatus::SPI_OK, shadow, size);
            }
            ASSERT_EQ(this->m_readPorts[this->fromPortHistory_readComplete->size() - 1], port);
        }
        completions++;
        ASSERT_EQ(this->fromPortHistory_readComplete->size() + this->fromPortHistory_writeComplete->size(),
                  completions);
        ASSERT_EQ(this->eventHistory_FramSpiError->size() > 0, fail || this->eventHistory_FramSpiError->size() > 0);
        if (completions % 32 == 0) {
            this->clearHistory();
            completions = 0;
        }
    }
    ASSERT_GT(accepted, 1000u);
}

// ----------------------------------------------------------------------
// From-port handlers
// ----------------------------------------------------------------------

void FramDriverTester ::from_spiWriteReadOut_handler(FwIndexType portNum,
                                                     Fw::Buffer& writeBuffer,
                                                     Fw::Buffer& readBuffer) {
    this->pushFromPortEntry_spiWriteReadOut(writeBuffer, readBuffer);
    ASSERT_EQ(writeBuffer.getSize(), readBuffer.getSize());
    this->m_lastWrite = writeBuffer;
    this->m_lastRead = readBuffer;
    if (this->m_spiCount < MAX_HISTORY_SIZE) {
        this->m_opcodes[this->m_spiCount] = writeBuffer.getData()[0];
    }
    this->m_spiCount++;
}

void FramDriverTester ::from_readComplete_handler(FwIndexType portNum,
                                                  U32 offset,
                                                  Fw::Buffer& buffer,
                                                  const Fram::Status& status) {
    const FwSizeType index = this->fromPortHistory_readComplete->size();
    this->pushFromPortEntry_readComplete(offset, buffer, status);
    if (index < MAX_HISTORY_SIZE) {
        this->m_readPorts[index] = portNum;
    }
    if (this->m_requestFromCallback) {
        // UT-19: the driver must be IDLE already
        this->m_callbackVerdict = this->write(1, 0x60, 4);
    }
}

void FramDriverTester ::from_writeComplete_handler(FwIndexType portNum,
                                                   U32 offset,
                                                   Fw::Buffer& buffer,
                                                   const Fram::Status& status) {
    const FwSizeType index = this->fromPortHistory_writeComplete->size();
    this->pushFromPortEntry_writeComplete(offset, buffer, status);
    if (index < MAX_HISTORY_SIZE) {
        this->m_writePorts[index] = portNum;
    }
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

void FramDriverTester ::startup(Drv::SpiStatus::T idStatus, const U8* id, Drv::SpiStatus::T srStatus, U8 sr) {
    this->invoke_to_schedIn(0, 0);
    ASSERT_EQ(this->m_spiCount, 1u);
    ASSERT_EQ(this->lastTx()[0], RDID);
    ASSERT_EQ(this->lastLen(), 1u + Fram::DEVICE_ID_SIZE);
    U8 rx[1 + Fram::DEVICE_ID_SIZE] = {0};
    std::memcpy(&rx[1], id, Fram::DEVICE_ID_SIZE);
    this->reply(idStatus, rx, sizeof(rx));
    ASSERT_EQ(this->m_spiCount, 2u);
    ASSERT_EQ(this->lastTx()[0], RDSR);
    ASSERT_EQ(this->lastLen(), 2u);
    U8 srx[2] = {0, sr};
    this->reply(srStatus, srx, 2);
    ASSERT_EQ(this->eventHistory_FramStartupComplete->size(), 1u);
}

void FramDriverTester ::reply(Drv::SpiStatus::T status, const U8* rx, U32 rxLen) {
    Fw::Buffer w = this->m_lastWrite;
    Fw::Buffer r = this->m_lastRead;
    if (rx != nullptr) {
        // the device echoes nothing meaningful during the header; data follows it
        const U32 len = (rxLen < r.getSize()) ? rxLen : static_cast<U32>(r.getSize());
        if (rxLen == r.getSize()) {
            std::memcpy(r.getData(), rx, len);
        } else {
            std::memcpy(r.getData() + HDR, rx, len);
        }
    }
    this->invoke_to_spiReplyIn(0, w, r, Drv::SpiStatus(status));
}

const U8* FramDriverTester ::lastTx() const {
    return this->m_lastWrite.getData();
}

U32 FramDriverTester ::lastLen() const {
    return static_cast<U32>(this->m_lastWrite.getSize());
}

void FramDriverTester ::assertHeader(U8 opcode, U32 offset, U32 len) {
    ASSERT_EQ(this->lastLen(), len);
    ASSERT_EQ(this->lastTx()[0], opcode);
    ASSERT_EQ(this->lastTx()[1], static_cast<U8>(offset >> 16));
    ASSERT_EQ(this->lastTx()[2], static_cast<U8>(offset >> 8));
    ASSERT_EQ(this->lastTx()[3], static_cast<U8>(offset));
}

Fram::Status FramDriverTester ::read(FwIndexType port, U32 offset, U32 size) {
    this->m_buffer[port].set(this->m_data[port], size);
    return this->invoke_to_read(port, offset, this->m_buffer[port]);
}

Fram::Status FramDriverTester ::write(FwIndexType port, U32 offset, U32 size) {
    this->m_buffer[port].set(this->m_data[port], size);
    return this->invoke_to_write(port, offset, this->m_buffer[port]);
}

void FramDriverTester ::finishRead(FwIndexType port, U32 offset, U32 size, const U8* data) {
    this->assertHeader(READ, offset, HDR + size);
    const FwSizeType before = this->fromPortHistory_readComplete->size();
    this->reply(Drv::SpiStatus::SPI_OK, data, size);
    ASSERT_EQ(this->fromPortHistory_readComplete->size(), before + 1);
    const FromPortEntry_readComplete& e = this->fromPortHistory_readComplete->at(before);
    ASSERT_EQ(e.offset, offset);
    ASSERT_EQ(e.status, Fram::Status(Fram::Status::OK));
    ASSERT_EQ(e.buffer.getData(), this->m_data[port]);
    ASSERT_EQ(e.buffer.getSize(), size);
    ASSERT_EQ(std::memcmp(this->m_data[port], data, size), 0);
}

void FramDriverTester ::finishWrite(FwIndexType port, U32 offset, U32 size) {
    ASSERT_EQ(this->lastLen(), 1u);
    ASSERT_EQ(this->lastTx()[0], WREN);
    const FwSizeType before = this->fromPortHistory_writeComplete->size();
    this->reply(Drv::SpiStatus::SPI_OK);
    this->assertHeader(WRITE, offset, HDR + size);
    ASSERT_EQ(std::memcmp(&this->lastTx()[HDR], this->m_data[port], size), 0);
    ASSERT_EQ(this->fromPortHistory_writeComplete->size(), before);
    this->reply(Drv::SpiStatus::SPI_OK);
    ASSERT_EQ(this->fromPortHistory_writeComplete->size(), before + 1);
    const FromPortEntry_writeComplete& e = this->fromPortHistory_writeComplete->at(before);
    ASSERT_EQ(e.offset, offset);
    ASSERT_EQ(e.status, Fram::Status(Fram::Status::OK));
    ASSERT_EQ(e.buffer.getData(), this->m_data[port]);
    ASSERT_EQ(e.buffer.getSize(), size);
}

}  // namespace Fram
