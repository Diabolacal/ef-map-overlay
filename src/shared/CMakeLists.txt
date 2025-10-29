set(target_name ef_overlay_shared)

add_library(${target_name}
    overlay_schema.cpp
    overlay_events.cpp
    event_channel.cpp
    event_queue_channel.cpp
    shared_memory_channel.cpp
    star_catalog.cpp
)

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${target_name}
    PUBLIC
        nlohmann_json::nlohmann_json
    PRIVATE
        spdlog::spdlog
        kernel32
)
