// ======================================================================
// \title  FramDriver.hpp
// \brief  hpp file for FramDriver component implementation class
// ======================================================================

#ifndef Fram_FramDriver_HPP
#define Fram_FramDriver_HPP

#include "Fw/Buffer/Buffer.hpp"
#include "Fw/Types/StringTemplate.hpp"
#include "fprime-samd/Devices/Fram/Components/FramDriver/FramDriverComponentAc.hpp"
#include "fprime-samd/Devices/Fram/FramConfig/FppConstantsAc.hpp"
#include "fprime-samd/Devices/Fram/Types/FppConstantsAc.hpp"
#include "fprime-samd/Devices/Fram/Types/StatusEnumAc.hpp"

namespace Fram {

//! Protocol driver for the CY15B102QN SPI F-RAM.
//!
//! A request is validated and either rejected synchronously (reply from inside the request
//! call) or turned into one (READ) or two (WREN, WRITE) full-duplex SPI frames. The SPI reply
//! arrives in the DMAC ISR, where only the outcome is latched: the WRITE that follows a WREN
//! is launched, and the client's reply is delivered, from activeIn in main context. Nothing is
//! issued to the SPI driver from inside its own completion callback -- the DMAC driver emits
//! that callback before it has finished its channel bookkeeping.
class FramDriver final : public FramDriverComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct FramDriver object
    explicit FramDriver(const char* const compName  //!< The component name
    );

    //! Destroy FramDriver object
    ~FramDriver();

    FramDriver(const FramDriver&) = delete;
    FramDriver& operator=(const FramDriver&) = delete;

    //! CY15B102QN opcodes (datasheet "Opcode" table); one opcode per chip-select cycle
    static constexpr U8 OPCODE_WREN = 0x06;   //!< Set the write-enable latch
    static constexpr U8 OPCODE_WRITE = 0x02;  //!< Write memory data
    static constexpr U8 OPCODE_READ = 0x03;   //!< Read memory data
    static constexpr U8 OPCODE_RDID = 0x9F;   //!< Read device ID

    //! Bytes in the device ID returned by RDID
    static constexpr FwSizeType DEVICE_ID_SIZE = 9;

    //! A device ID rendered as hex; sized independently of FW_FIXED_LENGTH_STRING_SIZE
    using HexString = Fw::StringTemplate<2 * DEVICE_ID_SIZE + 1>;

    //! Bytes in the largest READ/WRITE frame: opcode, address, then MaxTransfer data bytes
    static constexpr FwSizeType FRAME_SIZE = Fram::HEADER_SIZE + Fram::Config::MaxTransfer;

  private:
    //! Where the single in-flight transaction is
    enum class State : U8 {
        IDLE,             //!< No transaction; requests are accepted
        READ_IN_FLIGHT,   //!< READ frame on the bus
        WREN_IN_FLIGHT,   //!< WREN frame on the bus
        WRITE_PENDING,    //!< WREN succeeded; the WRITE frame goes out on the next activeIn
        WRITE_IN_FLIGHT,  //!< WRITE frame on the bus
        REPLY_PENDING     //!< Outcome latched by the ISR; the client reply goes out on activeIn
    };

    //! Which client port family the in-flight transaction replies on
    enum class Operation : U8 { READ, WRITE, IDENTIFY };  //!< IDENTIFY: startup RDID, no client

    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for activeIn
    //!
    //! Main-context step of a transaction: launches the WRITE frame once its WREN has
    //! completed, or delivers the pending client reply. Returns true when it did either.
    bool activeIn_handler(FwIndexType portNum,  //!< The port number
                          U32 context           //!< The call order
                          ) override;

    //! Handler implementation for read
    //!
    //! Read buffer.size bytes from offset into buffer
    void read_handler(FwIndexType portNum,  //!< The port number
                      U32 offset,           //!< Byte offset into the F-RAM
                      Fw::Buffer& buffer    //!< Destination of the data
                      ) override;

    //! Handler implementation for spiReplyIn
    //!
    //! SPI completion. Arrives in DMAC ISR context: only the outcome is latched here.
    void spiReplyIn_handler(FwIndexType portNum,  //!< The port number
                            Fw::Buffer& writeBuffer,
                            Fw::Buffer& readBuffer,
                            const Drv::SpiStatus& status) override;

    //! Handler implementation for write
    //!
    //! Write buffer.size bytes from buffer to offset (WREN, then WRITE)
    void write_handler(FwIndexType portNum,  //!< The port number
                       U32 offset,           //!< Byte offset into the F-RAM
                       Fw::Buffer& buffer    //!< Source of the data
                       ) override;

    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! Check a request against the device bounds, the transfer limit and the busy state
    Fram::Status admit(U32 offset, const Fw::Buffer& buffer) const;

    //! Answer a request that was not started, synchronously, on the requesting port
    void reject(Operation operation, FwIndexType portNum, U32 offset, Fw::Buffer& buffer, Fram::Status status);

    //! Record the accepted request and lay out the opcode + address header in m_tx
    void begin(Operation operation, FwIndexType portNum, U32 offset, Fw::Buffer& buffer, U8 opcode);

    //! Hand a frame of frameSize bytes from tx (into rx) to the SPI driver
    void startTransfer(U8* tx, U8* rx, FwSizeType frameSize);

    //! Latch the outcome of the in-flight transaction for delivery on activeIn
    void finish(Fram::Status status);

    //! Stage and start the one-time RDID frame (REQ-FRAM-013)
    void identify();

    //! Compare the ID clocked in by RDID with Fram::EXPECTED_DEVICE_ID; event on difference
    void checkDeviceId();

    //! Render size bytes as upper-case hex into out
    static void toHex(const U8* bytes, FwSizeType size, HexString& out);

    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    //! Transaction state. Written from both main context and the SPI ISR; every transition
    //! is owned by exactly one of them (see the state machine in docs/sdd.md).
    volatile State m_state;
    bool m_identified;  //!< RDID has been issued (once, first activeIn in IDLE)

    Operation m_operation;              //!< Client family of the in-flight transaction
    FwIndexType m_portNum;              //!< Client port index of the in-flight transaction
    U32 m_offset;                       //!< F-RAM offset of the in-flight transaction
    Fw::Buffer m_clientBuffer;          //!< The client's buffer, returned untouched on the reply
    volatile Fram::Status::T m_status;  //!< Outcome latched by the ISR, delivered on activeIn

    U8 m_tx[FRAME_SIZE];  //!< READ/WRITE frame clocked out: opcode, address, data (write) or padding (read)
    U8 m_rx[FRAME_SIZE];  //!< Bytes clocked in; data of a read follows the HEADER_SIZE header echo
    U8 m_wrenTx[1];       //!< The one-byte WREN frame
    U8 m_wrenRx[1];       //!< Discarded byte clocked in during WREN

    Fw::Buffer m_spiWrite;  //!< Descriptor over the frame handed to the SPI driver
    Fw::Buffer m_spiRead;   //!< Descriptor over the receive staging handed to the SPI driver
};

}  // namespace Fram

#endif
