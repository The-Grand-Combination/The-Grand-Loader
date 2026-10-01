add_library(community_patch SHARED
    src/main.cpp
    src/config.cpp
    src/hooks/iat.cpp
    src/features/worker_threads.cpp
)

target_include_directories(community_patch PRIVATE src)
target_link_libraries(community_patch PRIVATE v2cp::plugin_api)
target_compile_definitions(community_patch PRIVATE V2CP_PATCH_VERSION="${PROJECT_VERSION}")
set_target_properties(community_patch PROPERTIES PREFIX "" OUTPUT_NAME "community_patch")

install(TARGETS community_patch RUNTIME DESTINATION mod/community_patch)
install(FILES mod/community_patch.mod DESTINATION mod)
