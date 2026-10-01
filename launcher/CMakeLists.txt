add_library(dinput8 SHARED
    src/dllmain.cpp
    src/proxy.cpp
    src/loader.cpp
    src/log.cpp
    src/modlist.cpp
    dinput8.def
)

target_include_directories(dinput8 PUBLIC include)
target_link_libraries(dinput8 PRIVATE shell32 psapi)
target_compile_definitions(dinput8 PRIVATE V2CP_LOADER_VERSION="${PROJECT_VERSION}")
set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME "dinput8")

# Header-only view of the plugin ABI for plugins to consume.
add_library(v2cp_plugin_api INTERFACE)
target_include_directories(v2cp_plugin_api INTERFACE include)
add_library(v2cp::plugin_api ALIAS v2cp_plugin_api)

install(TARGETS dinput8 RUNTIME DESTINATION .)
