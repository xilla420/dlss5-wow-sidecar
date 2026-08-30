add_executable(sidecar_tests
  test_smoke.cpp
  test_pattern.cpp
  ../src/testpattern/Pattern.cpp)
target_link_libraries(sidecar_tests PRIVATE sidecar_common Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(sidecar_tests)
