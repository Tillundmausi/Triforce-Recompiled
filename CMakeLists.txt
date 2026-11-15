cmake_minimum_required(VERSION 3.20)
project(TriForceHeroesRecomp VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TFH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(TFH_BUILD_TESTS "Build unit tests" ON)

function(tfh_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        if(TFH_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
        if(TFH_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# --- Phase 1: container formats + dump inspection --------------------------
add_library(tfh_formats STATIC
    src/common/sha256.cpp
    src/formats/blz.cpp
    src/formats/ctr.cpp
    src/formats/romfs.cpp
    src/inspect/inspector.cpp
    src/inspect/report.cpp
)
target_include_directories(tfh_formats PUBLIC src)
tfh_warnings(tfh_formats)

add_executable(ctrinspect tools/ctrinspect/main.cpp)
target_link_libraries(ctrinspect PRIVATE tfh_formats)
tfh_warnings(ctrinspect)

if(TFH_BUILD_TESTS)
    enable_testing()
    add_executable(tfh_tests tests/test_main.cpp)
    target_link_libraries(tfh_tests PRIVATE tfh_formats)
    tfh_warnings(tfh_tests)
    add_test(NAME tfh_tests COMMAND tfh_tests)
endif()
