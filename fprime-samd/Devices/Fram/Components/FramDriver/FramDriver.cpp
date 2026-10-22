// ======================================================================
// \title  FramDriver.cpp
// \brief  Protocol driver for the Infineon CY15B102QN SPI F-RAM (see docs/sdd.md)
// ======================================================================

#include "fprime-samd/Devices/Fram/Components/FramDriver/FramDriver.hpp"

#include <cstdio>
#include <cstring>

#include "Fw/Types/Assert.hpp"
#include "Fw/Types/StringTemplate.hpp"
#include "config/FwAssertArgTypeAliasAc.h"
#include "fprime-samd/Devices/Fram/FramConfig/FramDeviceIdConfig.hpp"

namespace Fram {

// REQ-FRAM-027: a configuration the driver cannot serve does not build
static_assert(Fram::Config::MaxTransfer >= 1, "Fram.Config.MaxTransfer must be at least 1");
static_assert(Fram::Config::MaxTransfer <= Fram::DEVICE_SIZE, "Fram.Config.MaxTransfer exceeds the device");
static_assert(FramDriver::FRAME_SIZE >= 1 + Fram::DEVICE_ID_SIZE, "staging must hold the RDID frame");
static_assert(Fram::Config::ClientPorts > 0, "Fram.Config.ClientPorts must be at least 1");
static_assert(sizeof(Fram::EXPECTED_DEVICE_ID) == Fram::DEVICE_ID_SIZE, "EXPECTED_DEVICE_ID size");

static_assert(Fram::HEADER_SIZE == 1 + Fram::ADDRESS_BYTES, "frame header is opcode + address");

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

FramDriver ::FramDriver(const char* const compName)
    : FramDriverComponentBase(compName),
      m_state(State::STARTUP),
      m_deviceIdMatch(false),
      m_portNum(0),
      m_offset(0),
      m_clientBuffer(),
      m_tx(),
      m_rx(),
      m_spiWrite(),
      m_spiRead() {}

FramDriver ::~FramDriver() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

Fram::Status FramDriver ::read_handler(FwIndexType portNum, U32 offset, Fw::Buffer& buffer) {
    const Fram::Status verdict = this->admit(buffer, offset);
    if (verdict != Fram::Status::OK) {
        return verdict;
    }
    this->record(portNum, offset, buffer, OPCODE_READ);
    // The data phase clocks zeros out while the device shifts the memory contents in
    std::memset(&this->m_tx[Fram::HEADER_SIZE], 0, buffer.getSize());
    this->issue(State::READ_IN_FLIGHT, static_cast<U32>(Fram::HEADER_SIZE) + buffer.getSize());
    return Fram::Status::OK;
}

Fram::Status FramDriver ::write_handler(FwIndexType portNum, U32 offset, Fw::Buffer& buffer) {
    const Fram::Status verdict = this->admit(buffer, offset);
    if (verdict != Fram::Status::OK) {
        return verdict;
    }
    // WREN first, as its own chip-select cycle (datasheet: the write-enable latch is set by
    // the WREN opcode alone); the WRITE frame is laid out when the WREN reply arrives.
    this->record(portNum, offset, buffer, OPCODE_WREN);
    this->issue(State::WREN_IN_FLIGHT, 1);
    return Fram::Status::OK;
}

void FramDriver ::spiReplyIn_handler(FwIndexType portNum,
                                     Fw::Buffer& writeBuffer,
                                     Fw::Buffer& readBuffer,
                                     const Drv::SpiStatus& status) {
    // REQ-FRAM-013: a reply that does not belong to the frame in flight is ignored, never
    // asserted on: no completion, no event, state unchanged.
    const bool inFlight = (this->m_state != State::STARTUP) && (this->m_state != State::IDLE);
    if (!inFlight || writeBuffer.getData() != this->m_tx || readBuffer.getData() != this->m_rx) {
        return;
    }
    switch (this->m_state) {
        case State::ID_IN_FLIGHT:
            this->onDeviceIdReply(status);
            break;
        case State::SR_IN_FLIGHT:
            this->onStatusRegisterReply(status);
            break;
        case State::READ_IN_FLIGHT:
            if (status != Drv::SpiStatus::SPI_OK) {
                this->fail(Fram::Operation::READ, status);
            } else {
                std::memcpy(this->m_clientBuffer.getData(), &this->m_rx[Fram::HEADER_SIZE],
                            this->m_clientBuffer.getSize());
                this->complete(Fram::Status::OK);
            }
            break;
        case State::WREN_IN_FLIGHT:
            if (status != Drv::SpiStatus::SPI_OK) {
                this->fail(Fram::Operation::WRITE_ENABLE, status);
            } else {
                // Same record, now the WRITE frame: opcode, address, then the client's data
                this->record(this->m_portNum, this->m_offset, this->m_clientBuffer, OPCODE_WRITE);
                std::memcpy(&this->m_tx[Fram::HEADER_SIZE], this->m_clientBuffer.getData(),
                            this->m_clientBuffer.getSize());
                this->issue(State::WRITE_IN_FLIGHT,
                            static_cast<U32>(Fram::HEADER_SIZE) + this->m_clientBuffer.getSize());
            }
            break;
        case State::WRITE_IN_FLIGHT:
            if (status != Drv::SpiStatus::SPI_OK) {
                this->fail(Fram::Operation::WRITE, status);
            } else {
                this->complete(Fram::Status::OK);
            }
            break;
        default:
            // Unreachable: inFlight excludes STARTUP and IDLE
            FW_ASSERT(false, static_cast<FwAssertArgType>(this->m_state));
    }
}

void FramDriver ::schedIn_handler(FwIndexType portNum, U32 context) {
    // REQ-FRAM-018: the first scheduled call starts the startup transactions; later calls
    // have nothing to do (replies arrive on spiReplyIn in main context).
    if (this->m_state == State::STARTUP) {
        std::memset(this->m_tx, 0, 1 + Fram::DEVICE_ID_SIZE);
        this->m_tx[0] = OPCODE_RDID;
        this->issue(State::ID_IN_FLIGHT, 1 + Fram::DEVICE_ID_SIZE);
    }
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

Fram::Status FramDriver ::admit(const Fw::Buffer& buffer, U32 offset) const {
    // A null data pointer is a programming error of the caller, not a request to reject
    FW_ASSERT(buffer.getData() != nullptr);
    const U32 size = static_cast<U32>(buffer.getSize());
    if (size == 0 || size > Fram::Config::MaxTransfer) {
        return Fram::Status::INVALID_SIZE;
    }
    // offset + size > DEVICE_SIZE without the addition overflowing
    if (offset > static_cast<U32>(Fram::DEVICE_SIZE) - size) {
        return Fram::Status::OUT_OF_RANGE;
    }
    if (this->m_state != State::IDLE) {
        return Fram::Status::BUSY;
    }
    return Fram::Status::OK;
}

void FramDriver ::record(FwIndexType portNum, U32 offset, const Fw::Buffer& buffer, U8 opcode) {
    this->m_portNum = portNum;
    this->m_offset = offset;
    this->m_clientBuffer = buffer;
    // Opcode, then the 3-byte address most significant byte first (datasheet "READ"/"WRITE")
    this->m_tx[0] = opcode;
    this->m_tx[1] = static_cast<U8>((offset >> 16) & 0xFF);
    this->m_tx[2] = static_cast<U8>((offset >> 8) & 0xFF);
    this->m_tx[3] = static_cast<U8>(offset & 0xFF);
}

void FramDriver ::issue(State next, U32 length) {
    FW_ASSERT(length >= 1 && length <= FRAME_SIZE, length);
    // State first: the host may answer (refuse) before this call returns, in main context
    this->m_state = next;
    this->m_spiWrite.set(this->m_tx, length);
    this->m_spiRead.set(this->m_rx, length);
    this->spiWriteReadOut_out(0, this->m_spiWrite, this->m_spiRead);
}

static void toHex(const U8* bytes, U32 count, Fw::StringTemplate<24>& out) {
    char text[2 * Fram::DEVICE_ID_SIZE + 1] = {};
    for (U32 i = 0; i < count && i < Fram::DEVICE_ID_SIZE; i++) {
        (void)std::snprintf(&text[2 * i], 3, "%02X", bytes[i]);
    }
    out = text;
}

void FramDriver ::onDeviceIdReply(const Drv::SpiStatus& status) {
    if (status != Drv::SpiStatus::SPI_OK) {
        this->m_deviceIdMatch = false;
        this->log_WARNING_LO_FramDeviceIdReadFailed(status);
    } else {
        // The ID follows the opcode echo: bytes 1 .. DEVICE_ID_SIZE of the receive frame
        const U8* got = &this->m_rx[1];
        this->m_deviceIdMatch = (std::memcmp(got, Fram::EXPECTED_DEVICE_ID, Fram::DEVICE_ID_SIZE) == 0);
        if (!this->m_deviceIdMatch) {
            Fw::StringTemplate<24> expected;
            Fw::StringTemplate<24> actual;
            toHex(Fram::EXPECTED_DEVICE_ID, Fram::DEVICE_ID_SIZE, expected);
            toHex(got, Fram::DEVICE_ID_SIZE, actual);
            this->log_WARNING_HI_FramDeviceIdMismatch(expected, actual);
        }
    }
    // Startup continues regardless (REQ-FRAM-015, -016): read the status register
    this->m_tx[0] = OPCODE_RDSR;
    this->m_tx[1] = 0;
    this->issue(State::SR_IN_FLIGHT, 2);
}

void FramDriver ::onStatusRegisterReply(const Drv::SpiStatus& status) {
    U8 statusRegister = 0xFF;
    bool writeProtected = false;
    if (status != Drv::SpiStatus::SPI_OK) {
        this->log_WARNING_LO_FramStatusReadFailed(status);
    } else {
        statusRegister = this->m_rx[1];
        writeProtected = (statusRegister & static_cast<U8>(Fram::STATUS_BLOCK_PROTECT_MASK)) != 0;
    }
    this->m_state = State::IDLE;
    this->log_ACTIVITY_LO_FramStartupComplete(this->m_deviceIdMatch, writeProtected, statusRegister);
}

void FramDriver ::complete(Fram::Status status) {
    // Copy the record out and free the driver before the client runs: the callback may issue
    // the next request from inside (REQ-FRAM-019)
    const FwIndexType portNum = this->m_portNum;
    const U32 offset = this->m_offset;
    Fw::Buffer buffer = this->m_clientBuffer;
    const bool isRead = (this->m_state == State::READ_IN_FLIGHT);
    this->m_state = State::IDLE;
    if (isRead) {
        this->readComplete_out(portNum, offset, buffer, status);
    } else {
        this->writeComplete_out(portNum, offset, buffer, status);
    }
}

void FramDriver ::fail(Fram::Operation::T operation, const Drv::SpiStatus& status) {
    this->log_WARNING_HI_FramSpiError(Fram::Operation(operation), status, this->m_offset,
                                      static_cast<U32>(this->m_clientBuffer.getSize()));
    this->complete(Fram::Status::SPI_ERROR);
}

}  // namespace Fram
