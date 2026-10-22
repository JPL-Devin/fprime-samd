// ======================================================================
// \title  FramDriver.hpp
// \brief  Protocol driver for the Infineon CY15B102QN SPI F-RAM (see docs/sdd.md)
// ======================================================================

#ifndef Fram_FramDriver_HPP
#define Fram_FramDriver_HPP

#include "Fw/Buffer/Buffer.hpp"
#include "config/FwIndexTypeAliasAc.h"
#include "fprime-samd/Devices/Fram/Components/FramDriver/FramDriverComponentAc.hpp"
#include "fprime-samd/Devices/Fram/FramConfig/FppConstantsAc.hpp"
#include "fprime-samd/Devices/Fram/Types/FppConstantsAc.hpp"

namespace Fram {

class FramDriver final : public FramDriverComponentBase {
  public:
    //! Longest SPI frame: opcode + 3 address bytes + MaxTransfer data bytes
    static constexpr U32 FRAME_SIZE = static_cast<U32>(Fram::HEADER_SIZE) + static_cast<U32>(Fram::Config::MaxTransfer);

    static constexpr U8 OPCODE_WREN = 0x06;   //!< Set the write-enable latch
    static constexpr U8 OPCODE_WRITE = 0x02;  //!< Write memory data
    static constexpr U8 OPCODE_READ = 0x03;   //!< Read memory data
    static constexpr U8 OPCODE_RDSR = 0x05;   //!< Read status register
    static constexpr U8 OPCODE_RDID = 0x9F;   //!< Read device ID

    explicit FramDriver(const char* const compName);
    ~FramDriver();

  private:
    //! Transaction state. Exactly one transfer is on the bus in any *_IN_FLIGHT state.
    enum class State : U8 {
        STARTUP,          //!< Before the first activeIn: nothing has been clocked
        ID_IN_FLIGHT,     //!< Startup RDID on the bus
        SR_IN_FLIGHT,     //!< Startup RDSR on the bus
        IDLE,             //!< Startup done, no client transaction in flight
        READ_IN_FLIGHT,   //!< Client READ on the bus
        WREN_IN_FLIGHT,   //!< Client write, WREN phase on the bus
        WRITE_IN_FLIGHT,  //!< Client write, WRITE phase on the bus
    };

    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    Fram::Status read_handler(FwIndexType portNum, U32 offset, Fw::Buffer& buffer) override;
    Fram::Status write_handler(FwIndexType portNum, U32 offset, Fw::Buffer& buffer) override;
    void spiReplyIn_handler(FwIndexType portNum,
                            Fw::Buffer& writeBuffer,
                            Fw::Buffer& readBuffer,
                            const Drv::SpiStatus& status) override;
    bool activeIn_handler(FwIndexType portNum, U32 context) override;

    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! Rejection precedence: null buffer asserts, then INVALID_SIZE, OUT_OF_RANGE, BUSY (REQ-FRAM-025)
    Fram::Status admit(const Fw::Buffer& buffer, U32 offset) const;

    //! Record the accepted request and lay out opcode + address in m_tx
    void record(FwIndexType portNum, U32 offset, const Fw::Buffer& buffer, U8 opcode);

    //! Hand a frame of `length` bytes (already in m_tx) to the SPI host; m_state is set first
    void issue(State next, U32 length);

    //! Startup RDID reply: compare with Fram::EXPECTED_DEVICE_ID, then issue RDSR
    void onDeviceIdReply(const Drv::SpiStatus& status);

    //! Startup RDSR reply: log FramStartupComplete and open for requests
    void onStatusRegisterReply(const Drv::SpiStatus& status);

    //! Finish the client transaction: state back to IDLE before the completion is invoked
    void complete(Fram::Status status);

    //! Log FramSpiError for the phase in flight and complete with SPI_ERROR
    void fail(Fram::Operation::T operation, const Drv::SpiStatus& status);

    State m_state;
    bool m_deviceIdMatch;  //!< Startup RDID matched (false when the read failed)

    //! The one client transaction in flight
    FwIndexType m_portNum;
    U32 m_offset;
    Fw::Buffer m_clientBuffer;

    //! SPI staging: frame clocked out (opcode, address, data or padding) and bytes clocked in
    U8 m_tx[FRAME_SIZE];
    U8 m_rx[FRAME_SIZE];
    Fw::Buffer m_spiWrite;  //!< Descriptor over m_tx handed to the SPI host
    Fw::Buffer m_spiRead;   //!< Descriptor over m_rx handed to the SPI host
};

}  // namespace Fram

#endif
