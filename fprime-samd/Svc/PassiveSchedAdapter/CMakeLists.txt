register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/PassiveSchedAdapter.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/PassiveSchedAdapter.cpp"
    DEPENDS
        fprime-samd_Svc_Types
)
