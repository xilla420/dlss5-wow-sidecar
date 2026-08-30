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

# Shaders are compiled to headers at build time so nothing is compiled at
# runtime and no .cso files need shipping.
find_program(DXC_EXECUTABLE dxc
  HINTS "$ENV{WindowsSdkVerBinPath}x64"
        "$ENV{WindowsSdkDir}bin/x64"
        "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "$ENV{ProgramFiles}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
if(NOT DXC_EXECUTABLE)
  message(FATAL_ERROR "dxc not found. Install the Windows SDK.")
endif()

set(SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/generated/gpu/Shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

function(compile_compute_shader name)
  add_custom_command(
    OUTPUT ${SHADER_OUT_DIR}/${name}.cs.h
    COMMAND ${DXC_EXECUTABLE} -T cs_6_0 -E CSMain
            -Fh ${SHADER_OUT_DIR}/${name}.cs.h
            -Vn g_${name}CS
            ${CMAKE_SOURCE_DIR}/src/common/gpu/Shaders/${name}.hlsl
    DEPENDS ${CMAKE_SOURCE_DIR}/src/common/gpu/Shaders/${name}.hlsl
    COMMENT "Compiling compute shader ${name}"
    VERBATIM)
  set(SHADER_HEADERS ${SHADER_HEADERS} ${SHADER_OUT_DIR}/${name}.cs.h PARENT_SCOPE)
endfunction()

compile_compute_shader(FormatNormalize)

add_library(sidecar_common STATIC)
target_include_directories(sidecar_common PUBLIC src/common)
target_link_libraries(sidecar_common PUBLIC
  d3d11 d3d12 dxgi dxguid dcomp windowsapp tomlplusplus::tomlplusplus)
target_sources(sidecar_common PRIVATE
  src/common/.keep.cpp
  src/common/core/GpuProfile.cpp
  src/common/core/LatencyStats.cpp
  src/common/gpu/DeviceBridge.cpp
  src/common/gpu/FormatNormalize.cpp
  src/common/capture/WgcSource.cpp
  src/common/present/DCompOverlay.cpp
  src/common/present/WindowTracker.cpp
  src/common/present/Hud.cpp
  src/common/neural/PassthroughPass.cpp)

# Generated shader headers, listed as sources so the custom commands run.
target_sources(sidecar_common PRIVATE ${SHADER_HEADERS})
target_include_directories(sidecar_common PUBLIC ${CMAKE_BINARY_DIR}/generated)
# d3dcompiler is deliberately not linked: shaders are compiled by dxc at build
# time and D3D12SerializeRootSignature lives in d3d12.dll, so linking it would
# add a DLL dependency this binary never calls.

# C++/WinRT needs the Windows Runtime headers and coroutine support.
target_compile_options(sidecar_common PUBLIC /await:strict)
target_link_libraries(sidecar_common PUBLIC windowsapp)

add_executable(wowsidecar WIN32
  src/runtime/main.cpp
  src/runtime/Pipeline.cpp)
target_include_directories(wowsidecar PRIVATE src)
target_link_libraries(wowsidecar PRIVATE sidecar_common shell32)
add_invariant_check(wowsidecar)

add_executable(testpattern WIN32
  src/testpattern/main.cpp
  src/testpattern/Pattern.cpp)
target_link_libraries(testpattern PRIVATE d3d11 dxgi)
add_invariant_check(testpattern)

enable_testing()
add_subdirectory(tests)
