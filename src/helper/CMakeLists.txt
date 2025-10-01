set(common_sources
    helper_server.cpp
    helper_runtime.cpp
    helper_websocket.cpp
    protocol_registration.cpp
    log_parsers.cpp
    log_watcher.cpp
    system_resolver.cpp
)

add_library(ef_overlay_helper_common STATIC ${common_sources})

if(MSVC)
    target_compile_definitions(ef_overlay_helper_common
        PUBLIC
            UNICODE
            _UNICODE
    )
endif()

target_compile_definitions(ef_overlay_helper_common
    PUBLIC
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(ef_overlay_helper_common
    PUBLIC
        kernel32
        user32
        ws2_32
        bcrypt
        spdlog::spdlog
        cpp-httplib::cpp-httplib
        nlohmann_json::nlohmann_json
        ef_overlay_shared
        asio
    shell32
    ole32
)

add_executable(ef_overlay_helper
    main.cpp
)

target_link_libraries(ef_overlay_helper
    PRIVATE
        ef_overlay_helper_common
)

set_target_properties(ef_overlay_helper
    PROPERTIES
        OUTPUT_NAME "ef-overlay-helper"
)

add_executable(ef_overlay_tray
    tray_main.cpp
    tray_application.cpp
)

target_link_libraries(ef_overlay_tray
    PRIVATE
        ef_overlay_helper_common
        shell32
        ole32
)

set_target_properties(ef_overlay_tray
    PROPERTIES
        OUTPUT_NAME "ef-overlay-tray"
    WIN32_EXECUTABLE TRUE
)
