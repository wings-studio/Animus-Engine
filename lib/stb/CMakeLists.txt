project(stb_utils)
add_library(stb_utils INTERFACE)
CPMAddPackage(
        NAME stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        DOWNLOAD_ONLY YES
)
target_include_directories(stb_utils INTERFACE ${stb_SOURCE_DIR})