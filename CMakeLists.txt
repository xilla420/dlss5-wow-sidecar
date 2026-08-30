cmake_minimum_required(VERSION 3.28)
project(dlss5-wow-sidecar LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Windows-only project (spec: Global Constraints).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# I12: no obfuscation, no packing, and a plain user-level manifest.
add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

include(FetchContent)
FetchContent_Declare(catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1)
FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0)
FetchContent_MakeAvailable(catch2 tomlplusplus)

# Our project-wide _UNICODE (above) would otherwise select Catch2's wmain entry
# point, which MSVC cannot auto-detect from inside a static library. This is
# Catch2's own documented opt-out.
target_compile_definitions(Catch2WithMain PRIVATE DO_NOT_USE_WMAIN)

include(cmake/Invariants.cmake)

add_library(sidecar_common STATIC)
target_include_directories(sidecar_common PUBLIC src/common)
target_link_libraries(sidecar_common PUBLIC
  d3d11 d3d12 dxgi dxguid dcomp windowsapp tomlplusplus::tomlplusplus)
target_sources(sidecar_common PRIVATE
  src/common/.keep.cpp
  src/common/core/GpuProfile.cpp
  src/common/core/LatencyStats.cpp
  src/common/gpu/DeviceBridge.cpp
  src/common/capture/WgcSource.cpp
  src/common/present/DCompOverlay.cpp
  src/common/neural/PassthroughPass.cpp)

# C++/WinRT needs the Windows Runtime headers and coroutine support.
target_compile_options(sidecar_common PUBLIC /await:strict)
target_link_libraries(sidecar_common PUBLIC windowsapp)

add_executable(wowsidecar WIN32 src/runtime/main.cpp)
target_link_libraries(wowsidecar PRIVATE sidecar_common)
add_invariant_check(wowsidecar)

add_executable(testpattern WIN32
  src/testpattern/main.cpp
  src/testpattern/Pattern.cpp)
target_link_libraries(testpattern PRIVATE d3d11 dxgi)
add_invariant_check(testpattern)

enable_testing()
add_subdirectory(tests)
