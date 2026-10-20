module Fram {
    @ Result of an F-RAM read or write request
    enum Status : U8 {
        OK           = 0 @< Transaction completed
        BUSY         = 1 @< Rejected: a transaction is already in flight
        OUT_OF_RANGE = 2 @< Rejected: offset + size exceeds the device
        TOO_LARGE    = 3 @< Rejected: size is 0 or exceeds Fram.Config.MaxTransfer
        SPI_ERROR    = 4 @< SPI driver reported a failure; transaction aborted
    }

    @ Infineon CY15B102QN / CY15V102QN: 2 Mbit = 256 KiB, byte addressed
    constant DEVICE_SIZE = 0x40000

    @ Address bytes clocked after the opcode, most significant first
    constant ADDRESS_BYTES = 3

    @ Opcode plus address bytes clocked before the data of a READ or WRITE
    constant HEADER_SIZE = 1 + ADDRESS_BYTES
}
