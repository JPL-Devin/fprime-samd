module Samd21 {
    @ Driver for the SAMD21 SPI Peripheral (host mode, DMA in both directions)
    passive component SpiDriver {

        @ An enum for selecting the two DMA channels on the SERCOM
        enum DmaChannel {
            MOSI,     @< DMA channel for transmission
            MISO,     @< DMA channel for receive
            N
        }

        @ Asynchronous SPI contract: SpiWriteRead[slot] starts one full-duplex transfer; every
        @ request is answered with exactly one SpiWriteReadComplete[slot] (SPI_OK, SPI_OTHER_ERR
        @ on a receive overrun, SPI_BUSY_ERR while another transfer is in flight, SPI_CONFIG_ERR
        @ before configure()). Completions are delivered from activeIn, never from an ISR and
        @ never from inside the request call.
        import Drv.AsyncGuardedSpi

        @ REQUIRED CONNECTION. Main-context delivery of the completions latched by the DMAC
        @ and SERCOM interrupts (and of refused requests). Connect to the deployment's
        @ Svc.PassiveCycler (cycleOut), at a lower index than the components that consume the
        @ completions. Always returns false: delivering replies leaves no work for this cycle.
        sync input port activeIn: Svc.ActiveSched

        @ Port to send a DMA transaction to the DMA driver
        output port dmaTransactionOut: [DmaChannel.N] Dma.Transaction

        @ Port to abort the DMA transaction still running on a channel. Used from the
        @ SERCOM error ISR when a receive overrun has made the transfer unable to finish.
        output port dmaTransactionAbortOut: [DmaChannel.N] Fw.Signal

        @ A signal from the DMAC that a request has finished.
        @ This signal comes inside an ISR! Only the outcome is latched here.
        sync input port dmaReplyIn: [DmaChannel.N] Dma.TransactionReply

        @ When the hardware chip select is disabled on this driver, this port controls
        @ the chip select pin through software. The port number is the device slot, i.e.
        @ the index of the SpiWriteRead / SpiWriteReadComplete ports.
        output port chipSelectGpioOut: [Drv.AsyncSpiCfg.SpiDriverPorts] Drv.GpioWrite

    }
}
