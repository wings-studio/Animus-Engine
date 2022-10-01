project(ImGui C CXX)

CPMAddPackage(
    NAME gh_imgui
    GITHUB_REPOSITORY SamCZ/imgui
    GIT_TAG 1f88c3f3cfaf7d5fed23096b32b61c9c225e35a7
)

set(SOURCE_FILES
        ${gh_imgui_SOURCE_DIR}/imgui.cpp
        ${gh_imgui_SOURCE_DIR}/imgui_demo.cpp
        ${gh_imgui_SOURCE_DIR}/imgui_draw.cpp
        ${gh_imgui_SOURCE_DIR}/imgui_tables.cpp
        ${gh_imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${gh_imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${gh_imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        )

if(NOT BUILD_SHARED_LIBS)
    add_library(ImGui STATIC ${SOURCE_FILES})
else()
    add_library(ImGui SHARED ${SOURCE_FILES})
    target_compile_definitions(ImGui PUBLIC IMGUI_SHARED=1)
    target_compile_definitions(ImGui PRIVATE IMGUI_EXPORT=1)
endif()

target_include_directories(ImGui PUBLIC ${gh_imgui_SOURCE_DIR})
target_link_libraries(ImGui glfw glad)

if(WIN32)
    target_link_libraries(ImGui gdi32 opengl32 imm32)
endif()