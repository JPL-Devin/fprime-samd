module Fram {
    @ Outcome of an F-RAM read or write request: the Request return value (OK = accepted, or a
    @ rejection) and the Reply status of an accepted request (OK or SPI_ERROR)
    enum Status : U8 {
        OK           = 0 @< Request accepted (Request return) / transaction completed (Reply)
        INVALID_SIZE = 1 @< Rejected: size is 0 or exceeds Fram.Config.MaxTransfer
        OUT_OF_RANGE = 2 @< Rejected: offset + size exceeds Fram.DEVICE_SIZE
        BUSY         = 3 @< Rejected: a transaction is in flight or the startup transactions have not completed
        SPI_ERROR    = 4 @< SPI host reported a failure; transaction aborted
    }

    @ F-RAM transaction being clocked when an SPI host error was reported (FramSpiError)
    enum Operation : U8 {
        READ         = 0 @< Memory read (READ 03h)
        WRITE_ENABLE = 1 @< Write enable preceding a memory write (WREN 06h)
        WRITE        = 2 @< Memory write (WRITE 02h)
    }

    @ Infineon CY15B102QN / CY15V102QN: 2 Mbit = 256 KiB, byte addressed
    constant DEVICE_SIZE = 0x40000

    @ Address bytes clocked after the opcode, most significant first
    constant ADDRESS_BYTES = 3

    @ Opcode plus address bytes clocked before the data of a READ or WRITE
    constant HEADER_SIZE = 1 + ADDRESS_BYTES

    @ Length of the device ID returned by RDID (9Fh)
    constant DEVICE_ID_SIZE = 9

    @ Block-protect bits BP1:BP0 of the status register (RDSR 05h); non-zero means part of
    @ the memory ignores writes
    constant STATUS_BLOCK_PROTECT_MASK = 0x0C
}
