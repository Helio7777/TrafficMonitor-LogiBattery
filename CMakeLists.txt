cmake_minimum_required(VERSION 3.20)
project(TrafficMonitorLogiBattery VERSION 1.0.0 LANGUAGES CXX)

add_library(LogiBatteryPlugin SHARED
    src/LogiBatteryPlugin.cpp
    src/LogitechHidpp.cpp
    src/LogitechHidpp.h
    src/TrafficMonitorPluginABI.h
    src/LogiBatteryPlugin.def
)

target_compile_features(LogiBatteryPlugin PRIVATE cxx_std_17)
target_compile_definitions(LogiBatteryPlugin PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0602
)

if(MSVC)
    target_compile_options(LogiBatteryPlugin PRIVATE /W4 /permissive- /utf-8 /EHsc)
    # Self-contained CRT so users do not need a separate VC++ runtime package.
    set_property(TARGET LogiBatteryPlugin PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(LogiBatteryPlugin PRIVATE setupapi hid ole32)

set_target_properties(LogiBatteryPlugin PROPERTIES
    OUTPUT_NAME "LogiBatteryPlugin"
)
