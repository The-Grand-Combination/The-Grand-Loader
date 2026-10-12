cmake_minimum_required(VERSION 3.25)

project(Victoria2CommunityPatches
    VERSION 0.1.0  # repository version; the loader and each plugin carry their own (see their CMakeLists)
    DESCRIPTION "Community Patches for Victoria II (v2game.exe 3.04)"
    LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "v2game.exe is a 32-bit Windows binary: configure with an x86 toolchain (e.g. an 'x86 Native Tools' prompt).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
endif()

# The loader and each plugin are released separately (one zip each, own version); this tree builds
# them together for development, and build/ (the install prefix) is the drop-in game layout.
option(V2CP_BUILD_LOADER "Build the loader (dinput8.dll)" ON)
option(V2CP_BUILD_COMMUNITY_PATCH "Build the community_patch plugin" ON)

add_subdirectory(launcher)  # always: it also defines the plugin ABI target (v2cp::plugin_api)
if(V2CP_BUILD_COMMUNITY_PATCH)
    add_subdirectory(patch)
endif()

# Release packages: `cpack --preset x86-release` writes one zip per component into out/release/:
#   v2cp-loader-<loader version>.zip        dinput8.dll + readme
#   community-patch-<patch version>.zip     mod/community_patch.ini + mod/community_patch/community_patch.dll
#   v2cp-plugin-sdk-<loader version>.zip    include/v2cp/plugin_api.h + PLUGINS.md
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "v2cp")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)  # zips extract straight into the game folder
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_SOURCE_DIR}/out/release")
set(CPACK_COMPONENTS_ALL)
if(V2CP_BUILD_LOADER)
    list(APPEND CPACK_COMPONENTS_ALL loader sdk)
    set(CPACK_ARCHIVE_LOADER_FILE_NAME "v2cp-loader-${V2CP_LOADER_VERSION}")
    set(CPACK_ARCHIVE_SDK_FILE_NAME "v2cp-plugin-sdk-${V2CP_LOADER_VERSION}")
endif()
if(V2CP_BUILD_COMMUNITY_PATCH)
    list(APPEND CPACK_COMPONENTS_ALL community_patch)
    set(CPACK_ARCHIVE_COMMUNITY_PATCH_FILE_NAME "community-patch-${COMMUNITY_PATCH_VERSION}")
endif()
include(CPack)
