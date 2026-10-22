// ======================================================================
// \title  FramDriverTester.hpp
// \brief  Unit tests for Fram::FramDriver (rows UT-xx of docs/sdd.md §3.1)
// ======================================================================

#ifndef Fram_FramDriverTester_HPP
#define Fram_FramDriverTester_HPP

#include "FramDriverGTestBase.hpp"
#include "fprime-samd/Devices/Fram/Components/FramDriver/FramDriver.hpp"
#include "fprime-samd/Devices/Fram/FramConfig/FramDeviceIdConfig.hpp"

namespace Fram {

class FramDriverTester final : public FramDriverGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 64;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static constexpr U32 MAX = static_cast<U32>(Fram::Config::MaxTransfer);
    static constexpr U32 HDR = static_cast<U32>(Fram::HEADER_SIZE);
    static constexpr U32 DEV = static_cast<U32>(Fram::DEVICE_SIZE);
    static constexpr FwIndexType N = FramDriver::NUM_READ_INPUT_PORTS;

    FramDriverTester();
    ~FramDriverTester();

    void testRead();                 // UT-01
    void testWrite();                // UT-02, UT-03
    void testAcceptThenComplete();   // UT-04, UT-10
    void testIndexRouting();         // UT-05
    void testInvalidSize();          // UT-07
    void testOutOfRange();           // UT-08
    void testBusy();                 // UT-09
    void testSpiErrors();            // UT-11
    void testNullBuffer();           // UT-12
    void testSpuriousReplies();      // UT-13
    void testDeviceId();             // UT-14, UT-16
    void testStartupReadFailures();  // UT-15, UT-28
    void testOpcodes();              // UT-17
    void testBeforeFirstSched();     // UT-18, UT-24
    void testRequestFromCallback();  // UT-19
    void testFootprint();            // UT-21
    void testPrecedence();           // UT-25
    void testWriteProtect();         // UT-26
    void testRandomScenario();       // UT-RB

  private:
    // from-port handlers
    void from_spiWriteReadOut_handler(FwIndexType portNum, Fw::Buffer& writeBuffer, Fw::Buffer& readBuffer) override;
    void from_readComplete_handler(FwIndexType portNum,
                                   U32 offset,
                                   Fw::Buffer& buffer,
                                   const Fram::Status& status) override;
    void from_writeComplete_handler(FwIndexType portNum,
                                    U32 offset,
                                    Fw::Buffer& buffer,
                                    const Fram::Status& status) override;

    // helpers
    void connectPorts();
    void initComponents();
    //! Run the startup transactions: RDID reply (idStatus, id bytes) then RDSR reply (srStatus, sr)
    void startup(Drv::SpiStatus::T idStatus = Drv::SpiStatus::SPI_OK,
                 const U8* id = Fram::EXPECTED_DEVICE_ID,
                 Drv::SpiStatus::T srStatus = Drv::SpiStatus::SPI_OK,
                 U8 sr = 0x00);
    //! Answer the last SPI request: fill the read frame from `rx` (if any) and reply `status`
    void reply(Drv::SpiStatus::T status, const U8* rx = nullptr, U32 rxLen = 0);
    //! The last SPI request's TX frame and length
    const U8* lastTx() const;
    U32 lastLen() const;
    //! Expect the last SPI request to be `opcode` + 3-byte `offset` with `len` total bytes
    void assertHeader(U8 opcode, U32 offset, U32 len);
    //! Issue a read/write on index `port` for offset/size using m_data[port]
    Fram::Status read(FwIndexType port, U32 offset, U32 size);
    Fram::Status write(FwIndexType port, U32 offset, U32 size);
    //! Complete a nominal read (one reply) / write (two replies) that is in flight
    void finishRead(FwIndexType port, U32 offset, U32 size, const U8* data);
    void finishWrite(FwIndexType port, U32 offset, U32 size);

    FramDriver component;
    Fw::Buffer m_lastWrite;
    Fw::Buffer m_lastRead;
    U32 m_spiCount;
    U8 m_data[N][MAX];
    Fw::Buffer m_buffer[N];
    FwIndexType m_readPorts[MAX_HISTORY_SIZE];
    FwIndexType m_writePorts[MAX_HISTORY_SIZE];
    U8 m_opcodes[MAX_HISTORY_SIZE];  //!< first TX byte of every SPI request, captured at issue time
    // UT-19: a request issued from inside the completion callback
    bool m_requestFromCallback;
    Fram::Status m_callbackVerdict;
};

}  // namespace Fram

#endif
