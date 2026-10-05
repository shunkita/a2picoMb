set(PROJECT_NAME a2picoMb)

cmake_minimum_required(VERSION 3.13...3.27)

if (CMAKE_BINARY_DIR MATCHES "build-w")
    if (NOT DEFINED PICO_BOARD)
        set(PICO_BOARD "pico2_w" CACHE STRING "Target board")
    endif ()
    if (EXISTS "/Users/oshuno/pico/pico-sdk-2.2.0")
        set(PICO_SDK_PATH "/Users/oshuno/pico/pico-sdk-2.2.0" CACHE PATH "Path to SDK" FORCE)
    endif ()
endif ()

include(pico_sdk_import.cmake)
project(${PROJECT_NAME} C CXX ASM)
pico_sdk_init()

if (PICO_CYW43_SUPPORTED OR CMAKE_BINARY_DIR MATCHES "build-w")
    set(TARGET_NAME "${PROJECT_NAME}-w")
elseif (CMAKE_BINARY_DIR MATCHES "build-th")
    set(TARGET_NAME "${PROJECT_NAME}-th")
else ()
    set(TARGET_NAME "${PROJECT_NAME}")
endif ()

add_executable(${TARGET_NAME})
pico_add_extra_outputs(${TARGET_NAME})

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

pico_enable_stdio_uart(${TARGET_NAME} 0)
pico_enable_stdio_usb(${TARGET_NAME} 1)

add_subdirectory(.. a2pico)

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
        CYW43_LWIP=0
        ENABLE_CLASSIC=1
        CYW43_USE_SPI=1
        USE_SDIOIT=0
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000
        )
