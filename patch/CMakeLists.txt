# performance_patch: the Victoria II Performance Patch, a plugin for The Grand Loader. Released on its own
# (it needs the loader installed); bump this version for patch releases.
project(performance_patch VERSION 1.0.0 LANGUAGES CXX)

add_library(performance_patch SHARED
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
    src/features/performance/country_prepass.cpp
    src/features/multiplayer/state_checksum.cpp
    src/features/multiplayer/oos_log.cpp
    src/features/multiplayer/fpu_pin.cpp
    src/features/multiplayer/oos_test.cpp
    src/features/multiplayer/sync_check.cpp
    src/features/multiplayer/world_rng.cpp
    src/features/multiplayer/host_wait.cpp
    src/features/performance/background_sleep.cpp
    src/features/stability/supply_guard.cpp
    src/features/stability/tech_window_guard.cpp
    src/features/stability/modifier_guard.cpp
    src/parallel/worker_pool.cpp
)

target_include_directories(performance_patch PRIVATE src)
target_link_libraries(performance_patch PRIVATE tgl::plugin_api winmm)
# Link map next to the DLL: sample_profiler writes our own leaves as offsets (cp+0x...), and
# docs/tools/scripts/annotate_samples.sh names them from this file.
target_link_options(performance_patch PRIVATE "/MAP:$<TARGET_FILE_DIR:performance_patch>/performance_patch.map")
target_compile_definitions(performance_patch PRIVATE PERFORMANCE_PATCH_VERSION="${PROJECT_VERSION}")
set_target_properties(performance_patch PROPERTIES PREFIX "" OUTPUT_NAME "performance_patch")

install(TARGETS performance_patch RUNTIME DESTINATION mod/performance_patch COMPONENT performance_patch)
install(FILES mod/performance_patch.ini DESTINATION mod COMPONENT performance_patch)

set(PERFORMANCE_PATCH_VERSION "${PROJECT_VERSION}" PARENT_SCOPE)
