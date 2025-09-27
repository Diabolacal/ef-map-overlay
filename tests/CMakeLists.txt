set(target_name ef_overlay_tests)

add_executable(${target_name}
    overlay_tests.cpp
)

target_link_libraries(${target_name}
    PRIVATE
        ef_overlay_shared
        nlohmann_json::nlohmann_json
)

add_test(NAME overlay-tests COMMAND ${target_name})
