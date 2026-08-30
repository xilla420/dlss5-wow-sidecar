# Runs ci/check_imports.py against a target's output after every build.
# A forbidden import fails the build, per spec I1/I4/I10.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

function(add_invariant_check target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/ci/check_imports.py
            $<TARGET_FILE:${target}>
    COMMENT "Checking ${target} against safety invariants I1/I4/I10"
    VERBATIM)
endfunction()
