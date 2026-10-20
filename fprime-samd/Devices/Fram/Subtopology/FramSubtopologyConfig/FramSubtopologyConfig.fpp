module Fram {
    @ Topology-level configuration of Fram.Subtopology. The SPI host settings live in the
    @ companion FramSubtopologyConfig.hpp.
    module SubtopologyConfig {
        @ Base id of the subtopology's instances. Neither instance has dictionary items, so
        @ this only needs to be unique within the deployment.
        constant BASE_ID = 0xDD00

        @ Samd21.SpiDriver slave slot (chip-select line) the F-RAM is attached to
        constant SPI_SLOT = 0
    }
}
