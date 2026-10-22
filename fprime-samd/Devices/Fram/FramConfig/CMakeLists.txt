# Component-level configuration of the Fram package (port counts, transfer limits, expected
# device ID). Deployments
# override these values with their own configuration module; see the F Prime configuration
# module documentation.
register_fprime_config(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/FramConfig.fpp"
    HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/FramDeviceIdConfig.hpp"
    INTERFACE
)
