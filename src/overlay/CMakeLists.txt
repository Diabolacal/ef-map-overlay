set(target_name ef_overlay_module)

add_library(${target_name} SHARED
    dllmain.cpp
    overlay_renderer.cpp
    overlay_hook.cpp
    starfield_renderer.cpp
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
        d3dcompiler
    minhook::minhook
    imgui
    spdlog::spdlog
    ef_overlay_shared
    cpp-httplib::cpp-httplib
)

set_target_properties(${target_name}
    PROPERTIES
        OUTPUT_NAME "ef-overlay"
)
