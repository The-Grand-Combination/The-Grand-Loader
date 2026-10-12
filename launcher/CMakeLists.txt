# The loader (dinput8.dll proxy) and the plugin ABI it owns. Released on its own: any plugin, not only
# community_patch, is loaded by it. Bump this version for loader releases; bump V2CP_PLUGIN_API_VERSION
# in include/v2cp/plugin_api.h only when the ABI changes.
project(v2cp_loader VERSION 0.1.0 LANGUAGES CXX)

# Header-only view of the plugin ABI for plugins to consume (always available, also when the loader
# itself is not built).
add_library(v2cp_plugin_api INTERFACE)
target_include_directories(v2cp_plugin_api INTERFACE include)
add_library(v2cp::plugin_api ALIAS v2cp_plugin_api)

# SDK package: the header and the plugin guide, for authors of other plugins. Not part of the game
# layout in build/.
install(FILES include/v2cp/plugin_api.h DESTINATION include/v2cp COMPONENT sdk EXCLUDE_FROM_ALL)
install(FILES PLUGINS.md DESTINATION . COMPONENT sdk EXCLUDE_FROM_ALL)

if(NOT V2CP_BUILD_LOADER)
    return()
endif()

add_library(dinput8 SHARED
    src/dllmain.cpp
    src/proxy.cpp
    src/loader.cpp
    src/log.cpp
    src/modlist.cpp
    src/version_label.cpp
    dinput8.def
)

target_include_directories(dinput8 PUBLIC include)
target_link_libraries(dinput8 PRIVATE shell32 psapi)
target_compile_definitions(dinput8 PRIVATE V2CP_LOADER_VERSION="${PROJECT_VERSION}")
set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME "dinput8")

install(TARGETS dinput8 RUNTIME DESTINATION . COMPONENT loader)
# The loader's own readme goes into its release zip only, not into the game layout in build/.
install(FILES README.md DESTINATION . RENAME v2cp-loader-README.md COMPONENT loader EXCLUDE_FROM_ALL)

set(V2CP_LOADER_VERSION "${PROJECT_VERSION}" PARENT_SCOPE)
