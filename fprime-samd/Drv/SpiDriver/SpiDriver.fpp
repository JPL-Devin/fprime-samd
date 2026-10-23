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
        @ request is answered with exactly one SpiWriteReadComplete[slot] (SPI_OK; SPI_OTHER_ERR
        @ on a receive overrun, a DMA bus error or a software chip select the GPIO driver refuses
        @ to drive; SPI_BUSY_ERR while a transfer is in flight on
        @ any slot, or while this slot's previous completion is still undelivered; SPI_CONFIG_ERR
        @ before configure()). Completions are delivered from activeIn, never from an ISR and
        @ never from inside the request call. A slot may have at most two requests outstanding
        @ (one transfer or completion plus one refusal) between activeIn turns; a third asserts.
        @ The guarded flavour of the interface (rather than Drv.AsyncSpi) puts an Os::Mutex
        @ around the request handler. On this single-context baremetal target every caller is
        @ already serialised by Svc.PassiveCycler, so the mutex never contends; a request from
        @ a second context (an ISR, or another task on an RTOS build) would be detected by it
        @ -- Os::Mutex::lock asserts on ERROR_BUSY -- rather than serialised. The state shared
        @ with the DMAC/SERCOM interrupts is protected by Samd21::CriticalSection inside the
        @ handlers, not by the guard.
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
