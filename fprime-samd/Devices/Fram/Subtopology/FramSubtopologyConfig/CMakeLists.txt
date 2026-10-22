# Subtopology configuration: base id, which SpiDriver slave slot the F-RAM is on, and the SPI
# host settings (SERCOM, pins, baud, mode). The expected device ID lives in FramConfig/. Deployments override by shipping their own copies of
# these two files in a configuration module.
register_fprime_config(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/FramSubtopologyConfig.fpp"
    HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/FramSubtopologyConfig.hpp"
    INTERFACE
)
