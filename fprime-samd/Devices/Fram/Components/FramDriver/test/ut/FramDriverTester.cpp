// ======================================================================
// \title  FramDriverTester.cpp
// \brief  cpp file for FramDriver component test harness implementation class
// ======================================================================

#include "FramDriverTester.hpp"
#include "fprime-samd/Devices/Fram/Subtopology/FramSubtopologyConfig/FramDeviceIdConfig.hpp"

#include <cstdio>
#include <cstring>

#include "STest/Pick/Pick.hpp"

namespace Fram {

static constexpr FwIndexType SPI_PORT = 0;
static constexpr FwIndexType SCHED_PORT = 0;
static constexpr U32 DEVICE_BYTES = static_cast<U32>(Fram::DEVICE_SIZE);
static constexpr FwSizeType MAX_TRANSFER = static_cast<FwSizeType>(Fram::Config::MaxTransfer);
static constexpr FwIndexType CLIENT_PORTS = static_cast<FwIndexType>(Fram::Config::ClientPorts);
static constexpr FwSizeType HDR_BYTES = static_cast<FwSizeType>(Fram::HEADER_SIZE);

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

FramDriverTester ::FramDriverTester(bool identified)
    : FramDriverGTestBase("FramDriverTester", FramDriverTester::MAX_HISTORY_SIZE),
      component("FramDriver"),
      m_clientData{},
      m_devicePattern{},
      m_lastTx{},
      m_lastFrameSize(0),
      m_lastWrite(),
      m_lastRead(),
      m_lastReadCompletePort(-1),
      m_lastWriteCompletePort(-1),
      m_spiRejectSynchronously(false),
      m_reenter(false),
      m_reenterPort(0),
      m_reenterOffset(0),
      m_reenterBuffer(),
      m_reenterData{} {
    this->initComponents();
    this->connectPorts();
    if (identified) {
        // Get the one-time startup RDID out of the way so the tests below see an idle driver
        this->startIdentification();
        this->completeIdentification(Fram::EXPECTED_DEVICE_ID);
        this->assertIdle();
    }
}

FramDriverTester ::~FramDriverTester() {
    this->component.deinit();
}

// ----------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------

void FramDriverTester ::testReadNominal() {
    const FwIndexType port = static_cast<FwIndexType>(STest::Pick::lowerUpper(0, CLIENT_PORTS - 1));
    const FwSizeType size = static_cast<FwSizeType>(STest::Pick::lowerUpper(1, MAX_TRANSFER));
    const U32 offset = STest::Pick::lowerUpper(0, DEVICE_BYTES - size);
    Fw::Buffer buffer = this->makeClientBuffer(size);

    this->request(true, port, offset, buffer);
    this->assertReadFrame(offset, size);
    this->assertNoReply();

    // Nothing to deliver until the SPI completes
    this->clearHistory();
    ASSERT_FALSE(this->invoke_to_activeIn(SCHED_PORT, 0));
    this->assertNoReply();

    for (FwSizeType i = 0; i < size; i++) {
        this->m_devicePattern[i] = static_cast<U8>(STest::Pick::any());
    }
    this->fillSpiRx(this->m_devicePattern, size);
    this->completeSpi(Drv::SpiStatus::SPI_OK);
    this->assertNoReply();  // ISR context never reaches the client

    this->deliver();
    this->assertReply(true, port, offset, buffer, Fram::Status::OK);
    ASSERT_EQ(0, std::memcmp(this->m_clientData, this->m_devicePattern, size));
    this->assertIdle();
}

void FramDriverTester ::testWriteNominal() {
    const FwIndexType port = static_cast<FwIndexType>(STest::Pick::lowerUpper(0, CLIENT_PORTS - 1));
    const FwSizeType size = static_cast<FwSizeType>(STest::Pick::lowerUpper(1, MAX_TRANSFER));
    const U32 offset = STest::Pick::lowerUpper(0, DEVICE_BYTES - size);
    Fw::Buffer buffer = this->makeClientBuffer(size);
    U8 original[FramDriver::FRAME_SIZE];
    (void)std::memcpy(original, this->m_clientData, size);

    this->request(false, port, offset, buffer);
    this->assertWrenFrame();
    this->assertNoReply();

    // The WREN completion (ISR context) issues nothing to the SPI driver and nothing to the
    // client; the driver stays busy until the WRITE goes out on the next activeIn.
    this->completeSpi(Drv::SpiStatus::SPI_OK);
    ASSERT_from_spiWriteReadOut_SIZE(0);
    this->assertNoReply();
    U8 otherData[2] = {0, 0};
    Fw::Buffer otherBuffer(otherData, sizeof otherData);
    this->request(true, port, 0, otherBuffer);
    this->assertRejected(true, port, 0, otherBuffer, Fram::Status::BUSY);

    this->launchWrite();
    this->assertWriteFrame(offset, buffer);
    this->assertNoReply();

    this->completeSpi(Drv::SpiStatus::SPI_OK);
    ASSERT_from_spiWriteReadOut_SIZE(0);
    this->assertNoReply();

    this->deliver();
    this->assertReply(false, port, offset, buffer, Fram::Status::OK);
    ASSERT_EQ(0, std::memcmp(this->m_clientData, original, size));
    this->assertIdle();
}

void FramDriverTester ::testRejections() {
    for (U32 pass = 0; pass < 2; pass++) {
        const bool isRead = (pass == 0);
        const FwIndexType port = static_cast<FwIndexType>(STest::Pick::lowerUpper(0, CLIENT_PORTS - 1));

        // Size 0
        Fw::Buffer empty = this->makeClientBuffer(0);
        this->request(isRead, port, 0, empty);
        this->assertRejected(isRead, port, 0, empty, Fram::Status::TOO_LARGE);

        // One past the transfer limit
        Fw::Buffer tooLarge = this->makeClientBuffer(MAX_TRANSFER + 1);
        this->request(isRead, port, 0, tooLarge);
        this->assertRejected(isRead, port, 0, tooLarge, Fram::Status::TOO_LARGE);

        // Largest transfer ending exactly at the end of the device is accepted
        Fw::Buffer maximal = this->makeClientBuffer(MAX_TRANSFER);
        this->runTransaction(isRead, port, DEVICE_BYTES - MAX_TRANSFER, MAX_TRANSFER, SpiFault::NONE);

        // One byte past the end of the device
        this->request(isRead, port, DEVICE_BYTES - MAX_TRANSFER + 1, maximal);
        this->assertRejected(isRead, port, DEVICE_BYTES - MAX_TRANSFER + 1, maximal, Fram::Status::OUT_OF_RANGE);

        // Offset at and beyond the device; the last one would wrap offset + size
        Fw::Buffer one = this->makeClientBuffer(1);
        this->request(isRead, port, DEVICE_BYTES, one);
        this->assertRejected(isRead, port, DEVICE_BYTES, one, Fram::Status::OUT_OF_RANGE);
        this->request(isRead, port, 0xFFFFFFFFU, one);
        this->assertRejected(isRead, port, 0xFFFFFFFFU, one, Fram::Status::OUT_OF_RANGE);

        // Both failures at once report the size failure
        this->request(isRead, port, DEVICE_BYTES, empty);
        this->assertRejected(isRead, port, DEVICE_BYTES, empty, Fram::Status::TOO_LARGE);

        this->assertIdle();
    }
}

void FramDriverTester ::testBusy() {
    const FwIndexType port = static_cast<FwIndexType>(STest::Pick::lowerUpper(0, CLIENT_PORTS - 1));
    const FwIndexType other = static_cast<FwIndexType>((port + 1) % CLIENT_PORTS);
    Fw::Buffer buffer = this->makeClientBuffer(8);
    U8 otherData[4] = {1, 2, 3, 4};
    Fw::Buffer otherBuffer(otherData, sizeof otherData);

    // In flight
    this->request(true, port, 0x100, buffer);
    this->assertReadFrame(0x100, 8);
    this->request(true, other, 0x200, otherBuffer);
    this->assertRejected(true, other, 0x200, otherBuffer, Fram::Status::BUSY);
    this->request(false, port, 0x200, otherBuffer);
    this->assertRejected(false, port, 0x200, otherBuffer, Fram::Status::BUSY);

    // Completed by the ISR but not yet delivered: still busy
    this->completeSpi(Drv::SpiStatus::SPI_OK);
    this->request(false, other, 0x200, otherBuffer);
    this->assertRejected(false, other, 0x200, otherBuffer, Fram::Status::BUSY);

    // The rejected requests did not disturb the one in flight
    this->deliver();
    this->assertReply(true, port, 0x100, buffer, Fram::Status::OK);
    this->assertIdle();

    // Accepting again
    this->runTransaction(false, other, 0x200, 4, SpiFault::NONE);
}

void FramDriverTester ::testSpiErrorRead() {
    const FwIndexType port = static_cast<FwIndexType>(STest::Pick::lowerUpper(0, CLIENT_PORTS - 1));
    this->runTransaction(true, port, 0x3FF00, 16, SpiFault::FIRST_FRAME);
}

void FramDriverTester ::testSpiErrorWren() {
    const FwIndexType port = static_cast<FwIndexType>(STest::Pick::lowerUpper(0, CLIENT_PORTS - 1));
    this->runTransaction(false, port, 0x1234, 16, SpiFault::FIRST_FRAME);
}

void FramDriverTester ::testSpiErrorWrite() {
    const FwIndexType port = static_cast<FwIndexType>(STest::Pick::lowerUpper(0, CLIENT_PORTS - 1));
    this->runTransaction(false, port, 0x1234, 16, SpiFault::SECOND_FRAME);
}

void FramDriverTester ::testSpiSynchronousReject() {
    const FwIndexType port = static_cast<FwIndexType>(STest::Pick::lowerUpper(0, CLIENT_PORTS - 1));
    Fw::Buffer buffer = this->makeClientBuffer(4);
    U8 original[4];
    (void)std::memcpy(original, this->m_clientData, sizeof original);

    // The SPI driver answers from inside spiWriteReadOut (e.g. busy with another slot)
    this->m_spiRejectSynchronously = true;
    this->request(true, port, 0x10, buffer);
    this->assertReadFrame(0x10, 4);
    this->assertNoReply();  // still not synchronous towards the client

    this->deliver();
    this->assertReply(true, port, 0x10, buffer, Fram::Status::SPI_ERROR);
    ASSERT_EQ(0, std::memcmp(this->m_clientData, original, sizeof original));
    this->assertIdle();

    // Same for the WREN of a write
    this->m_spiRejectSynchronously = true;
    this->request(false, port, 0x10, buffer);
    this->assertWrenFrame();
    this->assertNoReply();
    this->deliver();
    this->assertReply(false, port, 0x10, buffer, Fram::Status::SPI_ERROR);
    this->assertIdle();
    ASSERT_FALSE(this->m_spiRejectSynchronously);

    // And for the WRITE frame, which goes out from activeIn after a good WREN: the
    // synchronous refusal lands inside that activeIn call and is delivered on the next one
    this->request(false, port, 0x10, buffer);
    this->assertWrenFrame();
    this->completeSpi(Drv::SpiStatus::SPI_OK);
    this->m_spiRejectSynchronously = true;
    this->launchWrite();
    this->assertWriteFrame(0x10, buffer);
    ASSERT_FALSE(this->m_spiRejectSynchronously);
    this->deliver();
    this->assertReply(false, port, 0x10, buffer, Fram::Status::SPI_ERROR);
    this->assertIdle();
}

void FramDriverTester ::testReentrantRequest() {
    const FwIndexType port = static_cast<FwIndexType>(STest::Pick::lowerUpper(0, CLIENT_PORTS - 1));
    Fw::Buffer buffer = this->makeClientBuffer(4);

    this->m_reenter = true;
    this->m_reenterPort = static_cast<FwIndexType>((port + 1) % CLIENT_PORTS);
    this->m_reenterOffset = 0x2000;
    this->m_reenterBuffer.set(this->m_reenterData, 6);

    this->request(true, port, 0x10, buffer);
    this->completeSpi(Drv::SpiStatus::SPI_OK);
    this->deliver();

    // The first completion arrived and the request made from inside it went to the bus
    ASSERT_from_readComplete_SIZE(1);
    ASSERT_from_readComplete(0, 0x10, buffer, Fram::Status::OK);
    ASSERT_EQ(port, this->m_lastReadCompletePort);
    ASSERT_FALSE(this->m_reenter);
    this->assertReadFrame(0x2000, 6);

    this->completeSpi(Drv::SpiStatus::SPI_OK);
    this->deliver();
    this->assertReply(true, this->m_reenterPort, 0x2000, this->m_reenterBuffer, Fram::Status::OK);
    this->assertIdle();
}

void FramDriverTester ::testUnexpectedSpiReply() {
    U8 data[4] = {};
    Fw::Buffer writeBuffer(data, sizeof data);
    Fw::Buffer readBuffer(data, sizeof data);
    const Drv::SpiStatus status(Drv::SpiStatus::SPI_OK);
    ASSERT_DEATH_IF_SUPPORTED(this->invoke_to_spiReplyIn(SPI_PORT, writeBuffer, readBuffer, status), "");
}

void FramDriverTester ::testRandomTransactions() {
    for (U32 step = 0; step < RANDOM_STEPS; step++) {
        const bool isRead = (STest::Pick::lowerUpper(0, 1) == 0);
        const FwIndexType port = static_cast<FwIndexType>(STest::Pick::lowerUpper(0, CLIENT_PORTS - 1));
        const FwSizeType size = static_cast<FwSizeType>(STest::Pick::lowerUpper(1, MAX_TRANSFER));
        const U32 offset = STest::Pick::lowerUpper(0, DEVICE_BYTES - size);
        const U32 faultPick = STest::Pick::lowerUpper(0, isRead ? 1 : 2);
        const SpiFault fault = static_cast<SpiFault>(faultPick);
        this->runTransaction(isRead, port, offset, size, fault);
    }
}

// ----------------------------------------------------------------------
// Handlers for typed from ports
// ----------------------------------------------------------------------

void FramDriverTester ::from_readComplete_handler(FwIndexType portNum,
                                                  U32 offset,
                                                  Fw::Buffer& buffer,
                                                  const Fram::Status& status) {
    this->m_lastReadCompletePort = portNum;
    this->pushFromPortEntry_readComplete(offset, buffer, status);
    if (this->m_reenter) {
        this->m_reenter = false;
        this->invoke_to_read(this->m_reenterPort, this->m_reenterOffset, this->m_reenterBuffer);
    }
}

void FramDriverTester ::from_writeComplete_handler(FwIndexType portNum,
                                                   U32 offset,
                                                   Fw::Buffer& buffer,
                                                   const Fram::Status& status) {
    this->m_lastWriteCompletePort = portNum;
    this->pushFromPortEntry_writeComplete(offset, buffer, status);
}

void FramDriverTester ::from_spiWriteReadOut_handler(FwIndexType portNum,
                                                     Fw::Buffer& writeBuffer,
                                                     Fw::Buffer& readBuffer) {
    EXPECT_EQ(SPI_PORT, portNum);
    // The SAMD21 driver requires equal-length frames (full duplex DMA)
    EXPECT_EQ(writeBuffer.getSize(), readBuffer.getSize());
    EXPECT_LE(writeBuffer.getSize(), FramDriver::FRAME_SIZE);
    this->m_lastFrameSize = writeBuffer.getSize();
    (void)std::memcpy(this->m_lastTx, writeBuffer.getData(), this->m_lastFrameSize);
    this->m_lastWrite = writeBuffer;
    this->m_lastRead = readBuffer;
    this->pushFromPortEntry_spiWriteReadOut(writeBuffer, readBuffer);
    if (this->m_spiRejectSynchronously) {
        this->m_spiRejectSynchronously = false;
        const Drv::SpiStatus status(Drv::SpiStatus::SPI_OTHER_ERR);
        this->invoke_to_spiReplyIn(SPI_PORT, writeBuffer, readBuffer, status);
    }
}

// ----------------------------------------------------------------------
// Helper functions
// ----------------------------------------------------------------------

Fw::Buffer FramDriverTester ::makeClientBuffer(FwSizeType size) {
    for (FwSizeType i = 0; i < sizeof this->m_clientData; i++) {
        this->m_clientData[i] = static_cast<U8>(STest::Pick::any());
    }
    return Fw::Buffer(this->m_clientData, size);
}

void FramDriverTester ::request(bool isRead, FwIndexType portNum, U32 offset, Fw::Buffer& buffer) {
    this->clearHistory();
    if (isRead) {
        this->invoke_to_read(portNum, offset, buffer);
    } else {
        this->invoke_to_write(portNum, offset, buffer);
    }
}

void FramDriverTester ::assertRejected(bool isRead,
                                       FwIndexType portNum,
                                       U32 offset,
                                       const Fw::Buffer& buffer,
                                       Fram::Status status) {
    ASSERT_from_spiWriteReadOut_SIZE(0);
    this->assertReply(isRead, portNum, offset, buffer, status);
}

void FramDriverTester ::assertFrame(FwSizeType frameSize, U8 opcode, U32 offset) {
    ASSERT_from_spiWriteReadOut_SIZE(1);
    ASSERT_EQ(frameSize, this->m_lastFrameSize);
    ASSERT_EQ(frameSize, this->m_lastRead.getSize());
    ASSERT_EQ(opcode, this->m_lastTx[0]);
    ASSERT_EQ(static_cast<U8>((offset >> 16) & 0xFF), this->m_lastTx[1]);
    ASSERT_EQ(static_cast<U8>((offset >> 8) & 0xFF), this->m_lastTx[2]);
    ASSERT_EQ(static_cast<U8>(offset & 0xFF), this->m_lastTx[3]);
}

void FramDriverTester ::assertWrenFrame() {
    ASSERT_from_spiWriteReadOut_SIZE(1);
    ASSERT_EQ(1, this->m_lastFrameSize);
    ASSERT_EQ(1, this->m_lastRead.getSize());
    ASSERT_EQ(FramDriver::OPCODE_WREN, this->m_lastTx[0]);
}

void FramDriverTester ::assertReadFrame(U32 offset, FwSizeType size) {
    this->assertFrame(HDR_BYTES + size, FramDriver::OPCODE_READ, offset);
    for (FwSizeType i = 0; i < size; i++) {
        ASSERT_EQ(0, this->m_lastTx[HDR_BYTES + i]) << "padding byte " << i;
    }
}

void FramDriverTester ::assertWriteFrame(U32 offset, const Fw::Buffer& data) {
    this->assertFrame(HDR_BYTES + data.getSize(), FramDriver::OPCODE_WRITE, offset);
    ASSERT_EQ(0, std::memcmp(&this->m_lastTx[HDR_BYTES], data.getData(), data.getSize()));
}

void FramDriverTester ::assertNoReply() {
    ASSERT_from_readComplete_SIZE(0);
    ASSERT_from_writeComplete_SIZE(0);
}

void FramDriverTester ::startIdentification() {
    this->clearHistory();
    ASSERT_TRUE(this->invoke_to_activeIn(SCHED_PORT, 0));
    this->assertNoReply();
    ASSERT_from_spiWriteReadOut_SIZE(1);
    ASSERT_EQ(1 + FramDriver::DEVICE_ID_SIZE, this->m_lastFrameSize);
    ASSERT_EQ(FramDriver::OPCODE_RDID, this->m_lastTx[0]);
    for (FwSizeType i = 1; i < this->m_lastFrameSize; i++) {
        ASSERT_EQ(0, this->m_lastTx[i]) << "RDID dummy byte " << i;
    }
    ASSERT_EVENTS_SIZE(0);
}

void FramDriverTester ::completeIdentification(const U8* deviceId) {
    ASSERT_EQ(1 + FramDriver::DEVICE_ID_SIZE, this->m_lastRead.getSize());
    U8* const rx = this->m_lastRead.getData();
    rx[0] = 0xFF;  // what the device shifts out while receiving the opcode
    (void)std::memcpy(&rx[1], deviceId, FramDriver::DEVICE_ID_SIZE);
    this->completeSpi(Drv::SpiStatus::SPI_OK);
    ASSERT_EVENTS_SIZE(0);  // nothing is logged from the ISR path
    this->deliver();
    this->assertNoReply();
}

void FramDriverTester ::testIdentifyMatch() {
    this->startIdentification();
    this->completeIdentification(Fram::EXPECTED_DEVICE_ID);
    ASSERT_EVENTS_SIZE(0);
    this->assertIdle();  // and no second RDID on the next tick
    this->runTransaction(true, 0, 0x10, 8, SpiFault::NONE);
    this->assertIdle();
}

void FramDriverTester ::testIdentifyMismatch() {
    U8 other[FramDriver::DEVICE_ID_SIZE];
    (void)std::memcpy(other, Fram::EXPECTED_DEVICE_ID, sizeof other);
    other[0] = 0x00;
    other[sizeof other - 1] = static_cast<U8>(other[sizeof other - 1] ^ 0x01);
    this->startIdentification();
    this->completeIdentification(other);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_FramDeviceIdMismatch_SIZE(1);
    char expectedHex[2 * FramDriver::DEVICE_ID_SIZE + 1] = {};
    char gotHex[2 * FramDriver::DEVICE_ID_SIZE + 1] = {};
    for (FwSizeType i = 0; i < FramDriver::DEVICE_ID_SIZE; i++) {
        (void)snprintf(&expectedHex[2 * i], 3, "%02X", Fram::EXPECTED_DEVICE_ID[i]);
        (void)snprintf(&gotHex[2 * i], 3, "%02X", other[i]);
    }
    // The autocoder serializes event strings to at most FW_LOG_STRING_MAX_SIZE characters
    // (FW_MIN with the declared size 24); compare what can actually arrive.
    const FwSizeType visible = FW_MIN(static_cast<FwSizeType>(FW_LOG_STRING_MAX_SIZE), 2 * FramDriver::DEVICE_ID_SIZE);
    expectedHex[visible] = '\0';
    gotHex[visible] = '\0';
    ASSERT_EVENTS_FramDeviceIdMismatch(0, expectedHex, gotHex);
    // Informational only: the driver is idle and keeps serving requests
    this->assertIdle();
    this->runTransaction(false, 1, 0x20, 4, SpiFault::NONE);
    this->runTransaction(true, 1, 0x20, 4, SpiFault::NONE);
    this->assertIdle();
}

void FramDriverTester ::testIdentifySpiError() {
    this->startIdentification();
    this->completeSpi(Drv::SpiStatus::SPI_OTHER_ERR);
    this->deliver();
    this->assertNoReply();
    ASSERT_EVENTS_SIZE(0);
    this->assertIdle();  // not identified, not retried
    this->runTransaction(true, 0, 0x30, 16, SpiFault::NONE);
    this->assertIdle();
}

void FramDriverTester ::testIdentifyBusy() {
    U8 data[4] = {1, 2, 3, 4};
    Fw::Buffer buffer(data, sizeof data);
    this->startIdentification();
    this->request(true, 0, 0x40, buffer);
    this->assertRejected(true, 0, 0x40, buffer, Fram::Status::BUSY);
    this->request(false, 1, 0x40, buffer);
    this->assertRejected(false, 1, 0x40, buffer, Fram::Status::BUSY);
    ASSERT_from_spiWriteReadOut_SIZE(0);  // nothing new on the bus
    this->completeIdentification(Fram::EXPECTED_DEVICE_ID);
    ASSERT_EVENTS_SIZE(0);
    this->assertIdle();
    this->runTransaction(true, 0, 0x40, 4, SpiFault::NONE);
}

void FramDriverTester ::assertIdle() {
    this->clearHistory();
    ASSERT_FALSE(this->invoke_to_activeIn(SCHED_PORT, 0));
    this->assertNoReply();
    ASSERT_from_spiWriteReadOut_SIZE(0);
}

void FramDriverTester ::completeSpi(Drv::SpiStatus::T status) {
    this->clearHistory();
    const Drv::SpiStatus spiStatus(status);
    this->invoke_to_spiReplyIn(SPI_PORT, this->m_lastWrite, this->m_lastRead, spiStatus);
}

void FramDriverTester ::fillSpiRx(const U8* pattern, FwSizeType size) {
    ASSERT_EQ(HDR_BYTES + size, this->m_lastRead.getSize());
    U8* const rx = this->m_lastRead.getData();
    // The header echo is whatever the device shifted while receiving the command
    for (FwSizeType i = 0; i < HDR_BYTES; i++) {
        rx[i] = 0xFF;
    }
    (void)std::memcpy(&rx[HDR_BYTES], pattern, size);
}

void FramDriverTester ::launchWrite() {
    this->clearHistory();
    ASSERT_TRUE(this->invoke_to_activeIn(SCHED_PORT, 0));
    this->assertNoReply();
}

void FramDriverTester ::deliver() {
    this->clearHistory();
    ASSERT_TRUE(this->invoke_to_activeIn(SCHED_PORT, 0));
}

void FramDriverTester ::assertReply(bool isRead,
                                    FwIndexType portNum,
                                    U32 offset,
                                    const Fw::Buffer& buffer,
                                    Fram::Status status) {
    if (isRead) {
        ASSERT_from_readComplete_SIZE(1);
        ASSERT_from_writeComplete_SIZE(0);
        const FromPortEntry_readComplete& entry = this->fromPortHistory_readComplete->at(0);
        ASSERT_EQ(portNum, this->m_lastReadCompletePort);
        ASSERT_EQ(offset, entry.offset);
        ASSERT_EQ(buffer.getData(), entry.buffer.getData());
        ASSERT_EQ(buffer.getSize(), entry.buffer.getSize());
        ASSERT_EQ(status, entry.status);
    } else {
        ASSERT_from_writeComplete_SIZE(1);
        ASSERT_from_readComplete_SIZE(0);
        const FromPortEntry_writeComplete& entry = this->fromPortHistory_writeComplete->at(0);
        ASSERT_EQ(portNum, this->m_lastWriteCompletePort);
        ASSERT_EQ(offset, entry.offset);
        ASSERT_EQ(buffer.getData(), entry.buffer.getData());
        ASSERT_EQ(buffer.getSize(), entry.buffer.getSize());
        ASSERT_EQ(status, entry.status);
    }
}

void FramDriverTester ::runTransaction(bool isRead, FwIndexType portNum, U32 offset, FwSizeType size, SpiFault fault) {
    Fw::Buffer buffer = this->makeClientBuffer(size);
    U8 original[FramDriver::FRAME_SIZE];
    (void)std::memcpy(original, this->m_clientData, size);

    this->request(isRead, portNum, offset, buffer);
    Fram::Status expected = Fram::Status::OK;

    if (isRead) {
        this->assertReadFrame(offset, size);
        if (fault == SpiFault::FIRST_FRAME) {
            this->completeSpi(Drv::SpiStatus::SPI_WRITE_ERR);
            expected = Fram::Status::SPI_ERROR;
        } else {
            for (FwSizeType i = 0; i < size; i++) {
                this->m_devicePattern[i] = static_cast<U8>(STest::Pick::any());
            }
            this->fillSpiRx(this->m_devicePattern, size);
            this->completeSpi(Drv::SpiStatus::SPI_OK);
        }
    } else {
        this->assertWrenFrame();
        if (fault == SpiFault::FIRST_FRAME) {
            this->completeSpi(Drv::SpiStatus::SPI_OTHER_ERR);
            ASSERT_from_spiWriteReadOut_SIZE(0);  // no WRITE without the latch
            expected = Fram::Status::SPI_ERROR;
        } else {
            this->completeSpi(Drv::SpiStatus::SPI_OK);
            ASSERT_from_spiWriteReadOut_SIZE(0);  // the WRITE is not issued from the ISR
            this->launchWrite();
            this->assertWriteFrame(offset, buffer);
            if (fault == SpiFault::SECOND_FRAME) {
                this->completeSpi(Drv::SpiStatus::SPI_WRITE_ERR);
                expected = Fram::Status::SPI_ERROR;
            } else {
                this->completeSpi(Drv::SpiStatus::SPI_OK);
            }
        }
    }
    ASSERT_from_spiWriteReadOut_SIZE(0);
    this->assertNoReply();

    this->deliver();
    this->assertReply(isRead, portNum, offset, buffer, expected);
    if (isRead && expected == Fram::Status::OK) {
        ASSERT_EQ(0, std::memcmp(this->m_clientData, this->m_devicePattern, size));
    } else {
        ASSERT_EQ(0, std::memcmp(this->m_clientData, original, size));
    }
    this->assertIdle();
}

}  // namespace Fram
