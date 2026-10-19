module Samd21 {
    @ Driver for the SAMD21 SPI Peripheral
    passive component SpiDriver {

        @ An enum for selecting the two DMA channels on the USART
        enum DmaChannel {
            MOSI,     @< DMA channel for transmission
            MISO,     @< DMA channel for receive
            N
        }

        @ Note! The SpiReply signal is sent on an ISR
        import Samd21.AsyncSpi

        @ Port to send a DMA transaction to the DMA driver
        output port dmaTransactionOut: [DmaChannel.N] Dma.Transaction

        @ Port to abort the DMA transaction still running on a channel. Used from the
        @ SERCOM error ISR when a receive overrun has made the transfer unable to finish.
        output port dmaTransactionAbortOut: [DmaChannel.N] Fw.Signal

        @ A signal from the DMAC that a request has finished.
        @ This signal comes inside an ISR!
        sync input port dmaReplyIn: [DmaChannel.N] Dma.TransactionReply

        @ When the hardware chip select is disabled on this driver, this port controls
        @ the chip select pin through software. The port number corresponds to the port
        @ from the AsyncSpi interface.
        output port chipSelectGpioOut: [Samd21.SpiPorts] Drv.GpioWrite

    }
}