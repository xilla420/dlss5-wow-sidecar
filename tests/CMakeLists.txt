add_executable(sidecar_tests
  test_smoke.cpp
  test_pattern.cpp
  test_gpu_profile.cpp
  test_latency_stats.cpp
  test_device_bridge.cpp
  test_wgc_source.cpp
  test_dcomp_overlay.cpp
  test_passthrough_pass.cpp
  test_pipeline_m0.cpp
  test_pipeline_rebuild.cpp
  test_window_tracker.cpp
  test_hud.cpp
  test_format_normalize.cpp
  test_luminance.cpp
  test_nvofa_flow.cpp
  test_motion_vector_math.cpp
  test_config.cpp
  test_probes.cpp
  test_neural_pass_factory.cpp
  test_panic_switch.cpp
  ../src/manager/Probes.cpp
  ../src/runtime/Pipeline.cpp
  ../src/testpattern/Pattern.cpp)
target_include_directories(sidecar_tests PRIVATE ../src)
target_link_libraries(sidecar_tests PRIVATE sidecar_common Catch2::Catch2WithMain)

# The capture test launches testpattern.exe, which CreateProcess resolves from
# the test binary's own directory.
add_dependencies(sidecar_tests testpattern)
add_custom_command(TARGET sidecar_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:testpattern> $<TARGET_FILE_DIR:sidecar_tests>
  VERBATIM)

include(Catch)
catch_discover_tests(sidecar_tests)
