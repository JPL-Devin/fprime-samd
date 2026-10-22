module Fram {
    @ Read or write request. buffer.size bytes are transferred at offset. Returns OK when the
    @ transaction was started (the outcome arrives later on the matching Reply, on the same
    @ index) or INVALID_SIZE / OUT_OF_RANGE / BUSY when it was rejected and no SPI transfer was
    @ issued. The buffer is owned by the caller and must stay untouched until it is returned,
    @ with the same data pointer and size, on the Reply. A null data pointer asserts.
    port Request(
        offset: U32           @< Byte offset into the F-RAM, 0 ... Fram.DEVICE_SIZE - 1; offset + buffer.size must not exceed Fram.DEVICE_SIZE
        ref buffer: Fw.Buffer @< Destination (read) or source (write) of the data; size 1 ... Fram.Config.MaxTransfer bytes
    ) -> Fram.Status

    @ Completion of an accepted Request, delivered from main context on the same port index
    @ that issued the request
    port Reply(
        offset: U32           @< Byte offset of the request
        ref buffer: Fw.Buffer @< The buffer given with the request
        $status: Fram.Status  @< OK or SPI_ERROR
    )
}
