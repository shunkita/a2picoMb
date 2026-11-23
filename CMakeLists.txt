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

set(VERSION "2026-11-21" CACHE STRING "Firmware release date/version" FORCE)
set(TARGET_NAME "${PROJECT_NAME}_${VERSION}")
set(TEST_TARGET_NAME "${TARGET_NAME}_test")

# Audio sink backend selection (defaults to BTSTACK)
set(A2PICOMB_AUDIO_BACKEND "BTSTACK" CACHE STRING "Audio sink backend (BTSTACK)")

function(a2picomb_configure_target tgt enable_mock)
    add_executable(${tgt})
    pico_add_extra_outputs(${tgt})

    target_include_directories(${tgt} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/infra
            ${CMAKE_CURRENT_SOURCE_DIR}/infra/audio
            ${CMAKE_CURRENT_SOURCE_DIR}/infra/bt
            ${CMAKE_CURRENT_SOURCE_DIR}/test
            )

    pico_enable_stdio_uart(${tgt} 0)
    pico_enable_stdio_usb(${tgt} 1)

    target_sources(${tgt} PRIVATE
            board.c
            main.c
            infra/audio/audio_sink.c
            )
    if (enable_mock)
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
            A2PICO2=1
            PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000
            SSI263_ENABLE_MOCK=$<BOOL:${enable_mock}>
            )

    if (A2PICOMB_AUDIO_BACKEND STREQUAL "BTSTACK")
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
    endif ()
endfunction()

# Production firmware (default target name: a2picoMb_<VERSION>.uf2)
a2picomb_configure_target(${TARGET_NAME} OFF)

# Standalone mock-speech unit test firmware (a2picoMb_<VERSION>_test.uf2)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/ssi263_mock.c")
    a2picomb_configure_target(${TEST_TARGET_NAME} ON)
endif ()
