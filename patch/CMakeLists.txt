add_library(community_patch SHARED
    src/main.cpp
    src/config.cpp
    src/crash_log.cpp
    src/hooks/iat.cpp
    src/hooks/memory.cpp
    src/hooks/hourly_tick.cpp
    src/features/profilers/year_timer.cpp
    src/features/performance/worker_threads.cpp
    src/features/performance/cb_cull.cpp
    src/features/profilers/tick_profiler.cpp
    src/features/performance/decision_cull.cpp
    src/features/profilers/sample_profiler.cpp
    src/features/performance/network_wait.cpp
    src/features/performance/relations_prefetch.cpp
    src/features/performance/pops_prefetch.cpp
    src/features/memory/flag_cache.cpp
    src/features/performance/ai_queue.cpp
    src/features/performance/events_parallel.cpp
    src/parallel/worker_pool.cpp
)

target_include_directories(community_patch PRIVATE src)
target_link_libraries(community_patch PRIVATE v2cp::plugin_api winmm)
# Link map next to the DLL: sample_profiler writes our own leaves as offsets (cp+0x...), and
# docs/tools/scripts/annotate_samples.sh names them from this file.
target_link_options(community_patch PRIVATE "/MAP:$<TARGET_FILE_DIR:community_patch>/community_patch.map")
target_compile_definitions(community_patch PRIVATE V2CP_PATCH_VERSION="${PROJECT_VERSION}")
set_target_properties(community_patch PROPERTIES PREFIX "" OUTPUT_NAME "community_patch")

install(TARGETS community_patch RUNTIME DESTINATION mod/community_patch)
install(FILES mod/community_patch.ini DESTINATION mod)
