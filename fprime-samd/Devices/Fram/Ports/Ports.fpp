module Fram {
    @ Read or write request. buffer.size bytes are transferred at offset. The buffer is owned by
    @ the caller and must stay untouched until it is returned, with the same data pointer and
    @ size, on the matching Reply.
    port Request(
        offset: U32           @< Byte offset into the F-RAM
        ref buffer: Fw.Buffer @< Destination (read) or source (write) of the data
    )

    @ Completion of a Request, delivered on the same port index that issued the request
    port Reply(
        offset: U32           @< Byte offset of the request
        ref buffer: Fw.Buffer @< The buffer given with the request
        $status: Fram.Status  @< Outcome of the request
    )
}
