module Fram {
    @ Compile-time configuration of Fram.FramDriver
    module Config {
        @ Number of client request/reply port pairs on FramDriver (one per client component)
        constant ClientPorts = 2

        @ Largest single read or write, in bytes. The driver's static SPI staging costs
        @ 2 * (MaxTransfer + HEADER_SIZE) bytes of RAM.
        constant MaxTransfer = 128
    }
}
