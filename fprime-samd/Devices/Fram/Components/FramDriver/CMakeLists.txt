register_fprime_module(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/FramDriver.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/FramDriver.cpp"
    DEPENDS
        fprime-samd_Devices_Fram_Types
        fprime-samd_Devices_Fram_Ports
        fprime-samd_Drv_Types
        fprime-samd_Devices_Fram_Subtopology_FramSubtopologyConfig
)

### Unit Tests ###
register_fprime_ut(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/FramDriver.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/FramDriverTestMain.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/FramDriverTester.cpp"
    DEPENDS
        STest
    UT_AUTO_HELPERS
)
