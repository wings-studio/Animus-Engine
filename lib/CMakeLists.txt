include(../cmake/CPM.cmake)

#------------------------------
# GLFW
# Desktop Window Library
#------------------------------
CPMAddPackage(
        NAME glfw
        GITHUB_REPOSITORY glfw/glfw
        GIT_TAG 3.3.2
        OPTIONS
        "GLFW_BUILD_TESTS OFF"
        "GLFW_BUILD_EXAMPLES OFF"
        "GLFW_BULID_DOCS OFF"
        "GLFW_INSTALL OFF"
        "GLFW_USE_HYBRID_HPG OFF"
)

if(NOT TARGET glfw AND NOT PLATFORM_ANDROID)
    set_property(TARGET glfw PROPERTY FOLDER "Libs")
endif()

#--------------------------------
# nlohmann JSON
# JSON for Modern C++
#--------------------------------
CPMAddPackage(
        NAME nlohmann_json
        GITHUB_REPOSITORY nlohmann/json
        VERSION 3.11.2
        OPTIONS
            "JSON_BuildTests OFF"
            "JSON_Install OFF"
)
if(NOT TARGET nlohmann_json)
    set_property(TARGET nlohmann_json PROPERTY FOLDER "Libs")
endif()

#------------------------------
# GLM
# Game mathematics library
#------------------------------
CPMAddPackage(
        NAME glm
        GITHUB_REPOSITORY g-truc/glm
        GIT_TAG 0.9.9.8
)
if(NOT TARGET glm)
    set_property(TARGET glm PROPERTY FOLDER "Libs")
endif()

#------------------------------
# GLAD
# OpenGL
#------------------------------
if(NOT TARGET glad AND NOT PLATFORM_ANDROID)
    add_subdirectory(glad)
    set_property(TARGET glad PROPERTY FOLDER "Libs")
endif()

#------------------------------
# FMOD
# Sound library
#------------------------------
if(AU_FMOD_SOUND)
    add_subdirectory(fmod)
    set_property(TARGET fmod PROPERTY FOLDER "Libs")
endif()

#------------------------------
# ImGui
# Debug window render lib
#------------------------------
if(NOT TARGET ImGui)
    add_subdirectory(imgui)
    set_property(TARGET ImGui PROPERTY FOLDER "Libs")
endif()

#------------------------------
# FreeType
# Font library
#------------------------------
CPMAddPackage(
        NAME freetype
        GIT_REPOSITORY https://github.com/aseprite/freetype2.git
        GIT_TAG VER-2-10-0
        VERSION 2.10.0
        OPTIONS
            "FT_WITH_BROTLI OFF"
            "FT_WITH_HARFBUZZ OFF"
            "CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON"
            "CMAKE_DISABLE_FIND_PACKAGE_BZip2 ON"
            "CMAKE_DISABLE_FIND_PACKAGE_PNG ON"
            "CMAKE_DISABLE_FIND_PACKAGE_HarfBuzz ON"
            "CMAKE_DISABLE_FIND_PACKAGE_BrotliDec ON"
)
if(freetype_ADDED)
    set_property(TARGET freetype PROPERTY FOLDER "Libs")
endif()

#------------------------------
# RmlUI
# UI render library
#------------------------------

CPMAddPackage(
    NAME RmlUI
    GITHUB_REPOSITORY SamCZ/RmlUi
    GIT_TAG master
    OPTIONS
        "BUILD_SAMPLES OFF"
        "BUILD_TESTING OFF"
        "NO_FONT_INTERFACE_DEFAULT OFF"
        "NO_THIRDPARTY_CONTAINERS ON"
)
if(NOT TARGET RmlUI)
    #set(CUSTOM_CONFIGURATION ON)
    #set(CUSTOM_LINK_LIBRARIES freetype)
    set_property(TARGET RmlCore PROPERTY FOLDER "Libs")
    set_property(TARGET RmlDebugger PROPERTY FOLDER "Libs")
endif()

#------------------------------
# Tracy
# something
#------------------------------
CPMAddPackage(
    NAME TracyClient
    GITHUB_REPOSITORY wolfpld/tracy
    GIT_TAG master
)
if(NOT TARGET TracyClient)
    if(AU_CPU_PROFILE)
        set(TRACY_ENABLE ON)
    else()
        set(TRACY_ENABLE OFF)
    endif()

    set(BUILD_SHARED_LIBS_CACHE ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)

    set_property(TARGET TracyClient PROPERTY FOLDER "GameLib")
    if(WIN32)
        target_link_libraries(TracyClient PUBLIC Ws2_32 dbghelp)
        target_compile_definitions(TracyClient PUBLIC NOMINMAX)
    endif()
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_CACHE})
    set_property(TARGET TracyClient PROPERTY FOLDER "Libs")
endif()

#------------------------------
# ASSIMP
# Model loading library
#------------------------------
CPMAddPackage(
        NAME assimp
        GITHUB_REPOSITORY assimp/assimp
        VERSION 5.2.5
        OPTIONS
            "ASM686 OFF"
            "AMD64 OFF"
            "ASSIMP_BUILD_ZLIB ON"
            "ASSIMP_BUILD_TESTS OFF"
            "ZLIB_AMD64 OFF"
            "ASSIMP_NO_EXPORT ON"
            "ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF"
            "ASSIMP_BUILD_FBX_IMPORTER ON"
            "ASSIMP_BUILD_GLTF_IMPORTER OFF"
            "ASSIMP_BUILD_COLLADA_IMPORTER OFF"
            "ASSIMP_BUILD_DRACO OFF"
            "ASSIMP_BUILD_ASSIMP_TOOLS OFF"
            "ASSIMP_INSTALL OFF"
)
if(NOT TARGET assimp)
    IF (CMAKE_BUILD_TYPE MATCHES Debug AND NOT PLATFORM_ANDROID)
        set(BUILD_SHARED_LIBS ON)
    ELSE()
        set(BUILD_SHARED_LIBS OFF)
    ENDIF()

    set(BUILD_SHARED_LIBS_CACHE ${BUILD_SHARED_LIBS})

    if(NOT BUILD_SHARED_LIBS)
        set(BUILD_SHARED_LIBS ON)
    endif()

    set_property(TARGET assimp PROPERTY FOLDER "Libs")
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_CACHE})
endif()

#-----
# stb
#-----
if(NOT TARGET stb_utils)
    add_subdirectory(stb)
    set_property(TARGET stb_utils PROPERTY FOLDER "Libs")
endif()

#-------------------
# Lua
# Scripting language
#-------------------
CPMAddPackage(
        NAME lua
        GIT_REPOSITORY https://github.com/lua/lua.git
        VERSION 5.3.5
        DOWNLOAD_ONLY YES
)

if (lua_ADDED)
    # lua has no CMake support, so we create our own target

    FILE(GLOB lua_sources ${lua_SOURCE_DIR}/*.c)
    list(REMOVE_ITEM lua_sources "${lua_SOURCE_DIR}/lua.c" "${lua_SOURCE_DIR}/luac.c")
    add_library(lua STATIC ${lua_sources})

    target_include_directories(lua
            PUBLIC
            $<BUILD_INTERFACE:${lua_SOURCE_DIR}>
            )

    set_property(TARGET lua PROPERTY FOLDER "Libs")
endif()