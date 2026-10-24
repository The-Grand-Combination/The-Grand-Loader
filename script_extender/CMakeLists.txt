# script_extender: the Victoria II Script Extender for TGC, a plugin for The Grand Loader. Released on its
# own (it needs the loader installed); bump this version for its releases.
project(script_extender VERSION 0.1.0 LANGUAGES CXX)

add_library(script_extender SHARED
    src/main.cpp
    src/config.cpp
    src/hooks/memory.cpp
    src/features/building_map_mode.cpp
    src/features/country_upgrade.cpp
    src/game/localisation.cpp
)

target_include_directories(script_extender PRIVATE src)
target_link_libraries(script_extender PRIVATE tgl::plugin_api shell32)
target_compile_definitions(script_extender PRIVATE SCRIPT_EXTENDER_VERSION="${PROJECT_VERSION}")
set_target_properties(script_extender PROPERTIES PREFIX "" OUTPUT_NAME "script_extender")

install(TARGETS script_extender RUNTIME DESTINATION mod/script_extender COMPONENT script_extender)
install(FILES mod/script_extender.ini DESTINATION mod COMPONENT script_extender)
install(DIRECTORY mod/script_extender/localisation DESTINATION mod/script_extender COMPONENT script_extender)

set(SCRIPT_EXTENDER_VERSION "${PROJECT_VERSION}" PARENT_SCOPE)
