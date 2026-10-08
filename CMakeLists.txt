cmake_minimum_required(VERSION 3.13...3.27)

# Fetch upstream a2pico library
include(FetchContent)
FetchContent_Declare(a2pico
        GIT_REPOSITORY https://github.com/oliverschmidt/a2pico.git
        GIT_TAG main
        )
FetchContent_MakeAvailable(a2pico)

if (NOT DEFINED PICO_SDK_PATH AND EXISTS "/Users/oshuno/pico/pico-sdk-2.2.0")
    set(PICO_SDK_PATH "/Users/oshuno/pico/pico-sdk-2.2.0" CACHE PATH "Path to SDK" FORCE)
endif ()

include(pico_sdk_import.cmake)

set(PROJECT_NAME a2picoMb)
project(${PROJECT_NAME} C CXX ASM)
pico_sdk_init()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif ()

# Output binary naming scheme following Oliver Schmidt's convention:
# <project>_A2Pico2LiteW_<version> (Oliver Schmidt uses release date YYYY-MM-DD)
set(VERSION "2026-10-08" CACHE STRING "Firmware release date/version")
set(TARGET_NAME "${PROJECT_NAME}_A2Pico2LiteW_${VERSION}")

add_executable(${TARGET_NAME})
pico_add_extra_outputs(${TARGET_NAME})

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

pico_enable_stdio_uart(${TARGET_NAME} 0)
pico_enable_stdio_usb(${TARGET_NAME} 1)

target_sources(${TARGET_NAME} PRIVATE
        audio.c
        ay_audio.c
        ayumi.c
        board.c
        bt_a2dp_source.c
        main.c
        via6522.c
        )

target_link_libraries(${TARGET_NAME} PRIVATE
        a2pico
        pico_multicore
        pico_stdlib
        pico_cyw43_arch_poll
        pico_btstack_cyw43
        pico_btstack_classic
        pico_btstack_sbc_encoder
        m
        )

target_compile_definitions(${TARGET_NAME} PRIVATE
        A2PICO2=1
        CYW43_LWIP=0
        ENABLE_CLASSIC=1
        CYW43_USE_SPI=1
        USE_SDIOIT=0
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000
        CYW43_WL_GPIO_LED_PIN=0
        )
