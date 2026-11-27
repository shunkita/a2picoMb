cmake_minimum_required(VERSION 3.13...3.27)

# Fetch upstream a2pico library
include(FetchContent)
FetchContent_Declare(a2pico
        GIT_REPOSITORY https://github.com/oliverschmidt/a2pico.git
        GIT_TAG main
        )
FetchContent_MakeAvailable(a2pico)

if (NOT DEFINED PICO_BOARD)
    set(PICO_BOARD "a2pico2" CACHE STRING "Target board" FORCE)
endif ()

include(pico_sdk_import.cmake)

set(PROJECT_NAME a2picoMb)
project(${PROJECT_NAME} C CXX ASM)
pico_sdk_init()

add_subdirectory(mb_core)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif ()

set(VERSION "2026-11-25" CACHE STRING "Firmware release date/version")

# Audio sink backend selection: BTSTACK (default), USB_DEVICE, or ALL
set(A2PICOMB_AUDIO_BACKEND "BTSTACK" CACHE STRING "Audio sink backend (BTSTACK, USB_DEVICE, ALL)")
set_property(CACHE A2PICOMB_AUDIO_BACKEND PROPERTY STRINGS BTSTACK USB_DEVICE ALL)

# Build standalone Core 0 Stage 0-2 self-test target alongside USB_DEVICE
option(USB_DEV_SELFTEST "Build standalone USB Audio Device self-test firmware (a2picoMb-USB-DEV-TEST_<VERSION>.uf2)" ON)

if (NOT A2PICOMB_AUDIO_BACKEND MATCHES "^(BTSTACK|USB_DEVICE|ALL)$")
    message(FATAL_ERROR "Unsupported A2PICOMB_AUDIO_BACKEND='${A2PICOMB_AUDIO_BACKEND}'. Expected BTSTACK, USB_DEVICE, or ALL.")
endif ()

function(a2picomb_configure_target tgt backend enable_mock)
    add_executable(${tgt})
    pico_add_extra_outputs(${tgt})

    target_include_directories(${tgt} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/infra
            ${CMAKE_CURRENT_SOURCE_DIR}/infra/audio
            ${CMAKE_CURRENT_SOURCE_DIR}/infra/bt
            )

    pico_enable_stdio_uart(${tgt} 0)

    target_sources(${tgt} PRIVATE
            board.c
            main.c
            infra/audio/audio_sink.c
            )
    if (enable_mock)
        target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
        target_sources(${tgt} PRIVATE test/ssi263_mock.c)
    endif ()

    target_link_libraries(${tgt} PRIVATE
            a2pico_mb_core
            a2pico
            pico_multicore
            pico_stdlib
            m
            )

    target_compile_definitions(${tgt} PRIVATE
            PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000
            SSI263_ENABLE_MOCK=$<BOOL:${enable_mock}>
            )

    if (backend STREQUAL "BTSTACK")
        pico_enable_stdio_usb(${tgt} 1)
        target_sources(${tgt} PRIVATE
                infra/audio/backend_btstack.c
                infra/bt/bt_a2dp_source.c
                )
        target_link_libraries(${tgt} PRIVATE
                pico_cyw43_arch_poll
                pico_btstack_cyw43
                pico_btstack_classic
                pico_btstack_sbc_encoder
                )
        target_compile_definitions(${tgt} PRIVATE
                A2PICOMB_AUDIO_BACKEND_BTSTACK=1
                CYW43_LWIP=0
                ENABLE_CLASSIC=1
                CYW43_USE_SPI=1
                USE_SDIOIT=0
                CYW43_WL_GPIO_LED_PIN=0
                CYW43_PIO_CLOCK_DIV=3
                )
    elseif (backend STREQUAL "USB_DEVICE")
        # Disable USB stdio to prevent conflict with TinyUSB audio device stack
        pico_enable_stdio_usb(${tgt} 0)
        target_include_directories(${tgt} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/infra/usb
                )
        target_sources(${tgt} PRIVATE
                infra/audio/backend_usb_device.c
                infra/audio/usb_dev_telemetry.c
                infra/audio/audio_ringbuf.c
                infra/usb/usb_descriptors.c
                )
        target_link_libraries(${tgt} PRIVATE
                tinyusb_device
                pico_unique_id
                )
        target_compile_definitions(${tgt} PRIVATE
                A2PICOMB_AUDIO_BACKEND_USB_DEVICE=1
                )
    else ()
        message(FATAL_ERROR "Unknown backend '${backend}' passed to a2picomb_configure_target")
    endif ()
endfunction()

# Bluetooth A2DP Source firmware (a2picoMb-BT_<VERSION>.uf2)
if (A2PICOMB_AUDIO_BACKEND STREQUAL "BTSTACK" OR A2PICOMB_AUDIO_BACKEND STREQUAL "ALL")
    set(BT_TARGET_NAME "${PROJECT_NAME}-BT_${VERSION}")
    a2picomb_configure_target(${BT_TARGET_NAME} "BTSTACK" OFF)

    # Standalone mock-speech unit test firmware (a2picoMb-BT_<VERSION>_test.uf2)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/ssi263_mock.c")
        a2picomb_configure_target("${BT_TARGET_NAME}_test" "BTSTACK" ON)
    endif ()
endif ()

# USB Audio Device firmware (a2picoMb-USB-DEV_<VERSION>.uf2)
if (A2PICOMB_AUDIO_BACKEND STREQUAL "USB_DEVICE" OR A2PICOMB_AUDIO_BACKEND STREQUAL "ALL")
    set(USB_DEV_TARGET_NAME "${PROJECT_NAME}-USB-DEV_${VERSION}")
    a2picomb_configure_target(${USB_DEV_TARGET_NAME} "USB_DEVICE" OFF)

    # Standalone Core 0 Stage 0-2 self-test firmware (a2picoMb-USB-DEV-TEST_<VERSION>.uf2)
    if (USB_DEV_SELFTEST AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/usb_dev_selftest.c")
        set(USB_DEV_TEST_TARGET_NAME "${PROJECT_NAME}-USB-DEV-TEST_${VERSION}")
        a2picomb_configure_target(${USB_DEV_TEST_TARGET_NAME} "USB_DEVICE" OFF)
        target_include_directories(${USB_DEV_TEST_TARGET_NAME} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/test
                )
        target_sources(${USB_DEV_TEST_TARGET_NAME} PRIVATE
                test/usb_dev_selftest.c
                )
        target_compile_definitions(${USB_DEV_TEST_TARGET_NAME} PRIVATE
                USB_DEV_SELFTEST=1
                )
    endif ()
endif ()
