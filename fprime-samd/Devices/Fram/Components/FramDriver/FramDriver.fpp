module Fram {
    @ Protocol driver for the Infineon CY15B102QN SPI F-RAM. Passive: a request returns
    @ immediately (OK = accepted, else a rejection status) and an accepted request completes
    @ asynchronously through the SPI host's Drv.AsyncGuardedSpi completion, which the host
    @ delivers in main context; the completion is forwarded on the matching client index.
    @ One transaction is in flight at a time. Client requests, spiReplyIn and activeIn must all
    @ run in the same execution context (no mutual exclusion inside).
    passive component FramDriver {
        # ----------------------------------------------------------------------
        # Client interface
        # ----------------------------------------------------------------------
        @ Read buffer.size bytes from offset into buffer; returns OK or a rejection status
        sync input port read: [Fram.Config.ClientPorts] Fram.Request

        @ Write buffer.size bytes from buffer to offset (WREN, then WRITE); returns OK or a rejection status
        sync input port write: [Fram.Config.ClientPorts] Fram.Request

        @ Completion of an accepted read, on the index that issued it (OK or SPI_ERROR)
        output port readComplete: [Fram.Config.ClientPorts] Fram.Reply

        @ Completion of an accepted write, on the index that issued it (OK or SPI_ERROR)
        output port writeComplete: [Fram.Config.ClientPorts] Fram.Reply

        match readComplete with read
        match writeComplete with write

        # ----------------------------------------------------------------------
        # SPI bus (client side of Drv.AsyncGuardedSpi)
        # ----------------------------------------------------------------------
        @ Full-duplex SPI frame to the SPI host; write and read frames are the same size.
        @ Every request is answered exactly once on spiReplyIn.
        output port spiWriteReadOut: Drv.SpiWriteReadRequest

        @ SPI completion or refusal, delivered by the SPI host in main context (never from an
        @ ISR, never from inside spiWriteReadOut). Drives the next phase of the transaction
        @ or the client completion. A reply that matches nothing in flight is ignored.
        sync input port spiReplyIn: Drv.SpiWriteReadCallback

        # ----------------------------------------------------------------------
        # Scheduling
        # ----------------------------------------------------------------------
        @ REQUIRED CONNECTION. Issues the startup transactions (RDID, then RDSR) on the first
        @ call; nothing is clocked before it. Connect to Svc.PassiveCycler (cycleOut) at a
        @ higher index than the SPI host's activeIn. Always returns false (no more work).
        sync input port activeIn: Svc.ActiveSched

        # ----------------------------------------------------------------------
        # Events (REQ-FRAM-011, -014, -015, -026, -028). No commands, telemetry or parameters:
        # the only thing the driver has to say travels on the completion ports.
        # ----------------------------------------------------------------------
        @ The SPI host reported a failure for a transfer of a client transaction; the
        @ transaction was aborted and its completion carries SPI_ERROR (REQ-FRAM-011)
        event FramSpiError(
            operation: Fram.Operation @< Transaction phase being clocked
            spiStatus: Drv.SpiStatus  @< Status reported by the SPI host
            offset: U32               @< Byte offset of the failed client transaction
            $size: U32                @< Size in bytes of the failed client transaction
        ) severity warning high format "FRAM SPI error during {} : {} at offset {} size {}"

        @ The device ID read at startup (RDID 9Fh) differs from Fram::EXPECTED_DEVICE_ID
        @ (FramConfig/FramDeviceIdConfig.hpp). Both IDs as 18 hexadecimal characters in wire order.
        @ Informational: reads and writes are not blocked by a mismatch (REQ-FRAM-014, -016).
        event FramDeviceIdMismatch(
            expected: string size 24 @< Configured ID (Fram::EXPECTED_DEVICE_ID)
            got: string size 24 @< ID read back from the device
        ) severity warning high format "FRAM device id mismatch, expected {} got {}"

        @ The SPI host reported a failure for the startup device-ID read; the part is left
        @ unidentified and the driver proceeds (REQ-FRAM-015)
        event FramDeviceIdReadFailed(
            spiStatus: Drv.SpiStatus @< Status reported by the SPI host
        ) severity warning low format "FRAM device id read failed: {}"

        @ The SPI host reported a failure for the startup status-register read; block
        @ protection is left unverified and the driver proceeds (REQ-FRAM-015)
        event FramStatusReadFailed(
            spiStatus: Drv.SpiStatus @< Status reported by the SPI host
        ) severity warning low format "FRAM status register read failed: {}"

        @ Startup transactions finished; the driver now accepts client requests (REQ-FRAM-018,
        @ -024, -026, -028). writeProtected = BP1 or BP0 set in RDSR: writes into the protected
        @ block are silently discarded by the device.
        event FramStartupComplete(
            deviceIdMatch: bool  @< RDID equalled Fram::EXPECTED_DEVICE_ID; false when the read failed (FramDeviceIdReadFailed then precedes this event)
            writeProtected: bool @< RDSR has BP1 or BP0 set; false when the read failed
            statusRegister: U8   @< RDSR value; 0xFF when the read failed (never a real value: bits 6-4 and 0 read 0; FramStatusReadFailed then precedes this event)
        ) severity activity low format "FRAM startup complete, id match {} write protected {} status register {x}"

        event port eventOut
        text event port textEventOut
        time get port timeGetOut
    }
}
