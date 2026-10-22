module Fram {
    @ Topology-level configuration of Fram.Subtopology. The SPI host settings live in the
    @ companion FramSubtopologyConfig.hpp.
    module SubtopologyConfig {
        @ Base id of the subtopology's instances. framDriver's five events are dictionary items at
        @ BASE_ID + 0 ... BASE_ID + 4; spiDriver's items (if any) start at BASE_ID + 0x100. The
        @ deployment must keep [BASE_ID, BASE_ID + 0x200) free of other instances' ids and may
        @ override this constant.
        constant BASE_ID = 0xDD00

        @ Samd21.SpiDriver slave slot (chip-select line) the F-RAM is attached to
        constant SPI_SLOT = 0
    }
}
