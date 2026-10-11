add_library(community_patch SHARED
    src/main.cpp
    src/config.cpp
    src/crash_log.cpp
    src/hooks/iat.cpp
    src/hooks/memory.cpp
    src/hooks/hourly_tick.cpp
    src/features/year_timer.cpp
    src/features/worker_threads.cpp
    src/features/cb_cull.cpp
    src/features/tick_profiler.cpp
    src/features/decision_cull.cpp
    src/features/sample_profiler.cpp
    src/features/network_wait.cpp
    src/features/relations_prefetch.cpp
    src/features/flag_cache.cpp
)

target_include_directories(community_patch PRIVATE src)
target_link_libraries(community_patch PRIVATE v2cp::plugin_api winmm)
target_compile_definitions(community_patch PRIVATE V2CP_PATCH_VERSION="${PROJECT_VERSION}")
set_target_properties(community_patch PROPERTIES PREFIX "" OUTPUT_NAME "community_patch")

install(TARGETS community_patch RUNTIME DESTINATION mod/community_patch)
install(FILES mod/community_patch.ini DESTINATION mod)
