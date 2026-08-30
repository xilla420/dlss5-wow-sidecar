add_executable(sidecar_tests
  test_smoke.cpp
  test_pattern.cpp
  test_gpu_profile.cpp
  test_latency_stats.cpp
  test_device_bridge.cpp
  ../src/testpattern/Pattern.cpp)
target_link_libraries(sidecar_tests PRIVATE sidecar_common Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(sidecar_tests)
