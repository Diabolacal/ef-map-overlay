set(target_name ef_overlay_injector)

add_executable(${target_name}
    injector.cpp
)

if(MSVC)
    target_compile_definitions(${target_name}
        PRIVATE
            UNICODE
            _UNICODE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )
endif()

set_target_properties(${target_name}
    PROPERTIES
        OUTPUT_NAME "ef-overlay-injector"
)

target_link_libraries(${target_name}
    PRIVATE
        kernel32
        psapi
)
