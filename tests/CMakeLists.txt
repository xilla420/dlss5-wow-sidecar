add_executable(sidecar_tests
  test_smoke.cpp
  test_pattern.cpp
  test_gpu_profile.cpp
  test_latency_stats.cpp
  test_device_bridge.cpp
  test_wgc_source.cpp
  ../src/testpattern/Pattern.cpp)
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
