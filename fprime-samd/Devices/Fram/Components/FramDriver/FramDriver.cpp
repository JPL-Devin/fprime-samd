// ======================================================================
// \title  FramDriver.cpp
// \brief  cpp file for FramDriver component implementation class
// ======================================================================

#include "fprime-samd/Devices/Fram/Components/FramDriver/FramDriver.hpp"
#include "fprime-samd/Devices/Fram/Subtopology/FramSubtopologyConfig/FramDeviceIdConfig.hpp"

#include <cstdio>
#include <cstring>

#include "Fw/Types/Assert.hpp"
#include "config/FwAssertArgTypeAliasAc.h"

namespace Fram {

// C++14: static constexpr members that are odr-used (the unit test binds them by reference)
// need an out-of-class definition.
constexpr U8 FramDriver::OPCODE_WREN;
constexpr U8 FramDriver::OPCODE_WRITE;
constexpr U8 FramDriver::OPCODE_READ;
constexpr U8 FramDriver::OPCODE_RDID;
constexpr FwSizeType FramDriver::DEVICE_ID_SIZE;
constexpr FwSizeType FramDriver::FRAME_SIZE;

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

FramDriver ::FramDriver(const char* const compName)
    : FramDriverComponentBase(compName),
      m_state(State::IDLE),
      m_identified(false),
      m_operation(Operation::READ),
      m_portNum(0),
      m_offset(0),
      m_clientBuffer(),
      m_status(Fram::Status::OK),
      m_tx{},
      m_rx{},
      m_wrenTx{OPCODE_WREN},
      m_wrenRx{},
      m_spiWrite(),
      m_spiRead() {}

FramDriver ::~FramDriver() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

bool FramDriver ::activeIn_handler(FwIndexType portNum, U32 context) {
    (void)portNum;  // One cycler drives this component
    (void)context;  // The cycler's call-order token is unused here

    if (this->m_state == State::IDLE && !this->m_identified) {
        // First tick with the bus idle: events and SPI are connected by now (REQ-FRAM-013)
        this->m_identified = true;
        this->identify();
        return true;
    }
    if (this->m_state == State::WRITE_PENDING) {
        // WREN completed in the ISR; the latch is set, clock the WRITE frame staged by
        // write_handler. Done here rather than from the SPI completion because the DMAC
        // driver delivers that completion before it has finished its channel bookkeeping,
        // so a transaction queued from inside it corrupts the channel's descriptor chain.
        this->m_state = State::WRITE_IN_FLIGHT;
        this->startTransfer(this->m_tx, this->m_rx, Fram::HEADER_SIZE + this->m_clientBuffer.getSize());
        return true;
    }

    if (this->m_state != State::REPLY_PENDING) {
        return false;
    }

    if (this->m_operation == Operation::IDENTIFY) {
        // No client behind the RDID frame; a SPI failure leaves the part unidentified
        const Fram::Status status = this->m_status;
        this->m_state = State::IDLE;
        if (status == Fram::Status::OK) {
            this->checkDeviceId();
        }
        return true;
    }
    const FwSizeType size = this->m_clientBuffer.getSize();
    if (this->m_operation == Operation::READ && this->m_status == Fram::Status::OK) {
        // The first HEADER_SIZE bytes clocked in are what the device shifted out while it was
        // receiving the opcode and address; the data starts after them.
        (void)std::memcpy(this->m_clientBuffer.getData(), &this->m_rx[Fram::HEADER_SIZE], size);
    }

    // Snapshot and go idle BEFORE replying: the client may issue its next request from
    // inside the reply, and that request must see an idle driver.
    const Operation operation = this->m_operation;
    const FwIndexType clientPort = this->m_portNum;
    const U32 offset = this->m_offset;
    Fw::Buffer buffer = this->m_clientBuffer;
    const Fram::Status status = this->m_status;
    this->m_state = State::IDLE;

    if (operation == Operation::READ) {
        if (this->isConnected_readComplete_OutputPort(clientPort)) {
            this->readComplete_out(clientPort, offset, buffer, status);
        }
    } else {
        if (this->isConnected_writeComplete_OutputPort(clientPort)) {
            this->writeComplete_out(clientPort, offset, buffer, status);
        }
    }
    return true;
}

void FramDriver ::read_handler(FwIndexType portNum, U32 offset, Fw::Buffer& buffer) {
    const Fram::Status status = this->admit(offset, buffer);
    if (status != Fram::Status::OK) {
        this->reject(Operation::READ, portNum, offset, buffer, status);
        return;
    }

    this->begin(Operation::READ, portNum, offset, buffer, OPCODE_READ);

    // The bytes clocked out while the data comes back are don't-care; zero them so the frame
    // is deterministic on the wire.
    const FwSizeType size = buffer.getSize();
    (void)std::memset(&this->m_tx[Fram::HEADER_SIZE], 0, size);

    this->m_state = State::READ_IN_FLIGHT;
    this->startTransfer(this->m_tx, this->m_rx, Fram::HEADER_SIZE + size);
}

void FramDriver ::write_handler(FwIndexType portNum, U32 offset, Fw::Buffer& buffer) {
    const Fram::Status status = this->admit(offset, buffer);
    if (status != Fram::Status::OK) {
        this->reject(Operation::WRITE, portNum, offset, buffer, status);
        return;
    }

    this->begin(Operation::WRITE, portNum, offset, buffer, OPCODE_WRITE);

    // Stage the data now, in main context, so the ISR only has to launch the frame.
    const FwSizeType size = buffer.getSize();
    (void)std::memcpy(&this->m_tx[Fram::HEADER_SIZE], buffer.getData(), size);

    // Every WRITE must be preceded by WREN in its own chip-select cycle; the device clears
    // the write-enable latch again when the WRITE completes.
    this->m_state = State::WREN_IN_FLIGHT;
    this->startTransfer(this->m_wrenTx, this->m_wrenRx, sizeof this->m_wrenTx);
}

void FramDriver ::spiReplyIn_handler(FwIndexType portNum,
                                     Fw::Buffer& writeBuffer,
                                     Fw::Buffer& readBuffer,
                                     const Drv::SpiStatus& status) {
    (void)portNum;  // One SPI driver slot drives this component

    // ISR context. A reply with nothing in flight is a driver or wiring defect, not an input.
    const State state = this->m_state;
    FW_ASSERT(state == State::READ_IN_FLIGHT || state == State::WREN_IN_FLIGHT || state == State::WRITE_IN_FLIGHT,
              static_cast<FwAssertArgType>(state));

    // The SPI driver hands back the descriptors it was given, so they must be our staging.
    const U8* const expectedTx = (state == State::WREN_IN_FLIGHT) ? this->m_wrenTx : this->m_tx;
    const U8* const expectedRx = (state == State::WREN_IN_FLIGHT) ? this->m_wrenRx : this->m_rx;
    FW_ASSERT(writeBuffer.getData() == expectedTx, static_cast<FwAssertArgType>(state));
    FW_ASSERT(readBuffer.getData() == expectedRx, static_cast<FwAssertArgType>(state));

    if (status.e != Drv::SpiStatus::SPI_OK) {
        // Includes the SPI driver's busy rejection. After a failed WREN the WRITE is not
        // attempted: without the latch set it would be silently ignored by the device.
        this->finish(Fram::Status::SPI_ERROR);
        return;
    }

    switch (state) {
        case State::WREN_IN_FLIGHT:
            // Latch is set; the WRITE frame goes out from activeIn, in main context.
            this->m_state = State::WRITE_PENDING;
            break;
        case State::READ_IN_FLIGHT:
        case State::WRITE_IN_FLIGHT:
            this->finish(Fram::Status::OK);
            break;
        default:
            // Excluded by the assert above
            break;
    }
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

Fram::Status FramDriver ::admit(U32 offset, const Fw::Buffer& buffer) const {
    const FwSizeType size = buffer.getSize();
    if (size == 0 || size > Fram::Config::MaxTransfer) {
        return Fram::Status::TOO_LARGE;
    }
    // Written so that offset + size cannot wrap
    if (offset >= Fram::DEVICE_SIZE || size > static_cast<FwSizeType>(Fram::DEVICE_SIZE - offset)) {
        return Fram::Status::OUT_OF_RANGE;
    }
    if (this->m_state != State::IDLE) {
        return Fram::Status::BUSY;
    }
    return Fram::Status::OK;
}

void FramDriver ::reject(Operation operation,
                         FwIndexType portNum,
                         U32 offset,
                         Fw::Buffer& buffer,
                         Fram::Status status) {
    if (operation == Operation::READ) {
        if (this->isConnected_readComplete_OutputPort(portNum)) {
            this->readComplete_out(portNum, offset, buffer, status);
        }
    } else {
        if (this->isConnected_writeComplete_OutputPort(portNum)) {
            this->writeComplete_out(portNum, offset, buffer, status);
        }
    }
}

void FramDriver ::begin(Operation operation, FwIndexType portNum, U32 offset, Fw::Buffer& buffer, U8 opcode) {
    // A non-empty buffer without storage is a defect in the client, not an input to validate.
    FW_ASSERT(buffer.getData() != nullptr, static_cast<FwAssertArgType>(portNum));

    this->m_operation = operation;
    this->m_portNum = portNum;
    this->m_offset = offset;
    this->m_clientBuffer = buffer;
    this->m_status = Fram::Status::OK;

    // Opcode, then the 18-bit address in three bytes, most significant first. The upper six
    // bits are zero because admit() bounded the offset to DEVICE_SIZE.
    this->m_tx[0] = opcode;
    this->m_tx[1] = static_cast<U8>((offset >> 16) & 0xFF);
    this->m_tx[2] = static_cast<U8>((offset >> 8) & 0xFF);
    this->m_tx[3] = static_cast<U8>(offset & 0xFF);
}

void FramDriver ::startTransfer(U8* tx, U8* rx, FwSizeType frameSize) {
    FW_ASSERT(frameSize >= 1 && frameSize <= FRAME_SIZE, static_cast<FwAssertArgType>(frameSize));
    this->m_spiWrite.set(tx, frameSize);
    this->m_spiRead.set(rx, frameSize);

    // The SPI driver replies through spiReplyIn: from the DMAC ISR when the frame completes,
    // or SYNCHRONOUSLY from inside this call if it is busy with another slot's transaction.
    this->spiWriteReadOut_out(0, this->m_spiWrite, this->m_spiRead);
}

void FramDriver ::identify() {
    static_assert(1 + DEVICE_ID_SIZE <= FRAME_SIZE, "RDID frame must fit the staging buffers");
    static_assert(sizeof(Fram::EXPECTED_DEVICE_ID) == DEVICE_ID_SIZE, "EXPECTED_DEVICE_ID is 9 bytes");
    this->m_operation = Operation::IDENTIFY;
    this->m_portNum = 0;
    this->m_offset = 0;
    this->m_clientBuffer = Fw::Buffer();
    this->m_status = Fram::Status::OK;
    // RDID: opcode, then DEVICE_ID_SIZE dummy bytes while the ID is clocked in
    this->m_tx[0] = OPCODE_RDID;
    (void)std::memset(&this->m_tx[1], 0, DEVICE_ID_SIZE);
    this->m_state = State::READ_IN_FLIGHT;
    this->startTransfer(this->m_tx, this->m_rx, 1 + DEVICE_ID_SIZE);
}

void FramDriver ::checkDeviceId() {
    // Byte 0 is what the device shifted out while receiving the opcode; the ID follows
    const U8* const got = &this->m_rx[1];
    if (std::memcmp(got, Fram::EXPECTED_DEVICE_ID, DEVICE_ID_SIZE) == 0) {
        return;
    }
    HexString expectedHex;
    HexString gotHex;
    toHex(Fram::EXPECTED_DEVICE_ID, DEVICE_ID_SIZE, expectedHex);
    toHex(got, DEVICE_ID_SIZE, gotHex);
    this->log_WARNING_HI_FramDeviceIdMismatch(expectedHex, gotHex);
}

void FramDriver ::toHex(const U8* bytes, FwSizeType size, HexString& out) {
    char text[2 * DEVICE_ID_SIZE + 1] = {};
    FW_ASSERT(size <= DEVICE_ID_SIZE, static_cast<FwAssertArgType>(size));
    for (FwSizeType i = 0; i < size; i++) {
        (void)snprintf(&text[2 * i], sizeof text - 2 * i, "%02X", bytes[i]);
    }
    out = text;
}

void FramDriver ::finish(Fram::Status status) {
    // Outcome first, state second: activeIn only looks at the outcome once it sees
    // REPLY_PENDING.
    this->m_status = status.e;
    this->m_state = State::REPLY_PENDING;
}

}  // namespace Fram
