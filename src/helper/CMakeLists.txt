set(target_name ef_overlay_helper)

add_executable(${target_name}
    helper_server.cpp
    protocol_registration.cpp
    main.cpp
)

if(MSVC)
    target_compile_definitions(${target_name}
        PRIVATE
            UNICODE
            _UNICODE
    )
endif()

target_compile_definitions(${target_name}
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(${target_name}
    PRIVATE
        kernel32
        user32
    ws2_32
    spdlog::spdlog
    cpp-httplib::cpp-httplib
    nlohmann_json::nlohmann_json
    ef_overlay_shared
)

set_target_properties(${target_name}
    PROPERTIES
        OUTPUT_NAME "ef-overlay-helper"
)
