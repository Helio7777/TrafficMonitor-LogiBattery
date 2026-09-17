cmake_minimum_required(VERSION 3.20)
project(TrafficMonitorLogiBattery VERSION 1.1.2 LANGUAGES CXX)

set(LOGIBATTERY_PROJECT_URL "" CACHE STRING "Public project homepage embedded in plugin metadata")

add_library(LogiBatteryPlugin SHARED
    src/LogiBatteryPlugin.cpp
    src/LogitechHidpp.cpp
    src/LogitechHidpp.h
    src/MchoseHid.cpp
    src/MchoseHid.h
    src/MouseBatteryTypes.h
    src/TrafficMonitorPluginABI.h
    src/LogiBatteryPlugin.def
)

target_compile_features(LogiBatteryPlugin PRIVATE cxx_std_17)
target_compile_definitions(LogiBatteryPlugin PRIVATE
    UNICODE
    _UNICODE
    INITGUID
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0602
)

if(LOGIBATTERY_PROJECT_URL)
    string(REPLACE "\"" "\\\"" LOGIBATTERY_PROJECT_URL_ESCAPED "${LOGIBATTERY_PROJECT_URL}")
    target_compile_definitions(LogiBatteryPlugin PRIVATE
        LOGIBATTERY_PROJECT_URL=L\"${LOGIBATTERY_PROJECT_URL_ESCAPED}\"
    )
endif()

if(MSVC)
    target_compile_options(LogiBatteryPlugin PRIVATE /W4 /permissive- /utf-8 /EHsc)
    # Self-contained CRT so users do not need a separate VC++ runtime package.
    set_property(TARGET LogiBatteryPlugin PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(LogiBatteryPlugin PRIVATE setupapi hid ole32 comctl32)

set_target_properties(LogiBatteryPlugin PROPERTIES
    OUTPUT_NAME "LogiBatteryPlugin"
)
