add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/FramSubtopologyConfig")

register_fprime_module(
    INTERFACE
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/Subtopology.fpp"
    HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/FramSubtopologyTopologyDefs.hpp"
    DEPENDS
        fprime-samd_Devices_Fram_Subtopology_FramSubtopologyConfig
        fprime-samd_Devices_Fram_Components_FramDriver
        fprime-samd_Drv_SpiDriver
)
