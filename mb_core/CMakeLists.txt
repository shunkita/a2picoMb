add_library(a2pico_mb_core INTERFACE)

target_sources(a2pico_mb_core INTERFACE
        src/mb_core.c
        src/via6522.c
        src/ayumi.c
        src/ay_audio.c
        )

target_include_directories(a2pico_mb_core INTERFACE
        include
        )

target_link_libraries(a2pico_mb_core INTERFACE
        pico_stdlib
        m
        )
