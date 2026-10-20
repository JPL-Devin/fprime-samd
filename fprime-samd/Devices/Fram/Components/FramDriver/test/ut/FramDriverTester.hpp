// ======================================================================
// \title  FramDriverTester.hpp
// \brief  hpp file for FramDriver component test harness implementation class
// ======================================================================

#ifndef Fram_FramDriverTester_HPP
#define Fram_FramDriverTester_HPP

#include "fprime-samd/Devices/Fram/Components/FramDriver/FramDriver.hpp"
#include "fprime-samd/Devices/Fram/Components/FramDriver/FramDriverGTestBase.hpp"

namespace Fram {

//! Exercises FramDriver through its ports only. The tester stands in for the client(s), the
//! SAMD21 SPI driver (recording every frame and replying on demand) and the cycler.
class FramDriverTester final : public FramDriverGTestBase {
  public:
    // ----------------------------------------------------------------------
    // Constants
    // ----------------------------------------------------------------------

    //! Maximum size of histories storing events, telemetry, and port outputs
    static const FwSizeType MAX_HISTORY_SIZE = 10;

    //! Instance ID supplied to the component instance under test
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    //! Number of transactions in the randomized test
    static const U32 RANDOM_STEPS = 1000;

    //! Where the mock SPI driver fails a transaction
    enum class SpiFault : U8 {
        NONE,         //!< Every frame succeeds
        FIRST_FRAME,  //!< READ, or the WREN of a write, reports an error from the ISR
        SECOND_FRAME  //!< The WRITE frame reports an error from the ISR (writes only)
    };

  public:
    // ----------------------------------------------------------------------
    // Construction and destruction
    // ----------------------------------------------------------------------

    //! Construct object FramDriverTester
    explicit FramDriverTester(bool identified = true);

    //! Destroy object FramDriverTester
    ~FramDriverTester();

  public:
    // ----------------------------------------------------------------------
    // Tests
    // ----------------------------------------------------------------------

    //! READ frame layout, data copy from the receive staging, same-index completion
    void testReadNominal();

    //! WREN then WRITE in separate frames, same-index completion, client data untouched
    void testWriteNominal();

    //! TOO_LARGE / OUT_OF_RANGE rejections, synchronous and without SPI traffic
    void testRejections();

    //! A second request while one is in flight, or awaiting delivery, is BUSY
    void testBusy();

    //! SPI failure of a READ: SPI_ERROR, client buffer untouched
    void testSpiErrorRead();

    //! SPI failure of the WREN: no WRITE frame is issued, SPI_ERROR
    void testSpiErrorWren();

    //! SPI failure of the WRITE frame: SPI_ERROR
    void testSpiErrorWrite();

    //! The SPI driver rejecting synchronously, from inside spiWriteReadOut, is an SPI_ERROR
    void testSpiSynchronousReject();

    //! A client issuing its next request from inside the completion callback is accepted
    void testReentrantRequest();

    //! An SPI reply with nothing in flight is an internal error and asserts
    void testUnexpectedSpiReply();

    //! Many random transactions (port, direction, offset, size, fault) end with an idle driver
    void testRandomTransactions();

    //! Startup RDID returning the configured ID: no event, driver idle, normal reads follow
    void testIdentifyMatch();

    //! Startup RDID returning another ID: one FramDeviceIdMismatch with both IDs as hex
    void testIdentifyMismatch();

    //! SPI failure on the RDID frame: no event, driver back to IDLE, normal reads follow
    void testIdentifySpiError();

    //! Client requests while the RDID frame is in flight are rejected BUSY
    void testIdentifyBusy();

  private:
    // ----------------------------------------------------------------------
    // Handlers for typed from ports
    // ----------------------------------------------------------------------

    //! Mock client: records the port index, optionally re-enters with a new read
    void from_readComplete_handler(FwIndexType portNum,
                                   U32 offset,
                                   Fw::Buffer& buffer,
                                   const Fram::Status& status) override;

    //! Mock client: records the port index
    void from_writeComplete_handler(FwIndexType portNum,
                                    U32 offset,
                                    Fw::Buffer& buffer,
                                    const Fram::Status& status) override;

    //! Mock SPI driver: snapshots the frame, optionally rejects synchronously
    void from_spiWriteReadOut_handler(FwIndexType portNum, Fw::Buffer& writeBuffer, Fw::Buffer& readBuffer) override;

  private:
    // ----------------------------------------------------------------------
    // Helper functions
    // ----------------------------------------------------------------------

    //! Fill the client buffer with a random pattern and size it
    Fw::Buffer makeClientBuffer(FwSizeType size);

    //! Issue a read or write request after clearing the histories
    void request(bool isRead, FwIndexType portNum, U32 offset, Fw::Buffer& buffer);

    //! Assert a request was answered synchronously with status and produced no SPI frame
    void assertRejected(bool isRead, FwIndexType portNum, U32 offset, const Fw::Buffer& buffer, Fram::Status status);

    //! Assert exactly one frame went out, of frameSize bytes, with the given header
    void assertFrame(FwSizeType frameSize, U8 opcode, U32 offset);

    //! Assert exactly the one-byte WREN frame went out
    void assertWrenFrame();

    //! Assert a READ frame for size bytes at offset with zero padding went out
    void assertReadFrame(U32 offset, FwSizeType size);

    //! Assert a WRITE frame carrying data went out
    void assertWriteFrame(U32 offset, const Fw::Buffer& data);

    //! Assert no completion has been delivered on either family
    void assertNoReply();

    //! Assert the driver is idle: activeIn has nothing to deliver
    void assertIdle();

    //! Tick an un-identified idle driver and check the RDID frame it puts on the bus
    void startIdentification();

    //! Complete the RDID frame with the given 9-byte ID and deliver the outcome on activeIn
    void completeIdentification(const U8* deviceId);

    //! Mock SPI driver completion of the last frame, from "ISR" context
    void completeSpi(Drv::SpiStatus::T status);

    //! Fill the receive staging of the last frame, after the header, with pattern
    void fillSpiRx(const U8* pattern, FwSizeType size);

    //! Run activeIn once after a successful WREN: it must launch the WRITE frame and reply to nobody
    void launchWrite();

    //! Run the cycler and assert a reply was delivered
    void deliver();

    //! Assert exactly one completion, on the right family and index, with these values
    void assertReply(bool isRead, FwIndexType portNum, U32 offset, const Fw::Buffer& buffer, Fram::Status status);

    //! Run a full transaction with the given fault, asserting every step, ending idle
    void runTransaction(bool isRead, FwIndexType portNum, U32 offset, FwSizeType size, SpiFault fault);

  private:
    // ----------------------------------------------------------------------
    // Helper methods
    // ----------------------------------------------------------------------

    //! Connect ports
    void connectPorts();

    //! Initialize components
    void initComponents();

  private:
    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    //! The component under test
    FramDriver component;

    //! Client storage, kept pristine around reads that fail and writes
    U8 m_clientData[FramDriver::FRAME_SIZE];

    //! Pattern the mock device "returns" on a read
    U8 m_devicePattern[FramDriver::FRAME_SIZE];

    //! Snapshot of the bytes of the last frame handed to the SPI driver
    U8 m_lastTx[FramDriver::FRAME_SIZE];
    FwSizeType m_lastFrameSize;
    Fw::Buffer m_lastWrite;
    Fw::Buffer m_lastRead;

    //! Index the last completion arrived on, per family
    FwIndexType m_lastReadCompletePort;
    FwIndexType m_lastWriteCompletePort;

    //! When set, the mock SPI driver answers the next frame synchronously with SPI_OTHER_ERR
    bool m_spiRejectSynchronously;

    //! When set, the mock client issues this read from inside the next readComplete
    bool m_reenter;
    FwIndexType m_reenterPort;
    U32 m_reenterOffset;
    Fw::Buffer m_reenterBuffer;
    U8 m_reenterData[FramDriver::FRAME_SIZE];
};

}  // namespace Fram

#endif
