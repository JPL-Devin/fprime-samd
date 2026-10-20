module Fram {
    @ Protocol driver for the Infineon CY15B102QN SPI F-RAM. Passive: a request returns
    @ immediately and completes asynchronously through the SAMD21 SPI driver's DMA reply.
    @ One transaction is in flight at a time; a request that cannot be started is answered
    @ synchronously, from within the request call, with a non-OK status.
    passive component FramDriver {

        # ----------------------------------------------------------------------
        # Client interface
        # ----------------------------------------------------------------------

        @ Read buffer.size bytes from offset into buffer
        sync input port read: [Fram.Config.ClientPorts] Fram.Request

        @ Write buffer.size bytes from buffer to offset (WREN, then WRITE)
        sync input port write: [Fram.Config.ClientPorts] Fram.Request

        @ Completion of a read, on the index that issued it
        output port readComplete: [Fram.Config.ClientPorts] Fram.Reply

        @ Completion of a write, on the index that issued it
        output port writeComplete: [Fram.Config.ClientPorts] Fram.Reply

        match readComplete with read
        match writeComplete with write

        # ----------------------------------------------------------------------
        # SPI bus
        # ----------------------------------------------------------------------

        @ Full-duplex SPI frame to Samd21.SpiDriver; write and read frames are the same size
        output port spiWriteReadOut: Samd21.SpiWriteRead

        @ SPI completion. Arrives in DMAC ISR context: only the outcome is latched here;
        @ nothing is issued back to the SPI driver from inside its completion callback.
        sync input port spiReplyIn: Samd21.SpiReply

        # ----------------------------------------------------------------------
        # Main-context delivery
        # ----------------------------------------------------------------------

        @ REQUIRED CONNECTION: connect to the deployment's Svc.PassiveCycler. Completions of
        @ accepted requests are delivered to clients from here, in main context, never from
        @ the SPI ISR. Returns true when a reply was delivered on this call.
        sync input port activeIn: Svc.ActiveSched

        # ----------------------------------------------------------------------
        # Startup device identification (REQ-FRAM-013..015)
        # ----------------------------------------------------------------------

        @ The RDID (9Fh) read issued on the first activeIn tick returned a device ID other
        @ than Fram::EXPECTED_DEVICE_ID (FramSubtopologyConfig.hpp). Both IDs as 9 bytes of
        @ hex. Informational: reads and writes are not blocked by a mismatch.
        event FramDeviceIdMismatch(
            expected: string size 24 @< Configured ID (Fram::EXPECTED_DEVICE_ID)
            got: string size 24 @< ID read back from the device
        ) severity warning high format "FRAM device id mismatch, expected {} got {}"

        event port eventOut
        text event port textEventOut
        time get port timeGetOut
    }
}
