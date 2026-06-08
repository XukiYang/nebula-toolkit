cmake_minimum_required(VERSION 3.14)
project(nebula-toolkit VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NEBULA_BUILD_TESTS "Build unit test executables under tests/unit" ON)
option(NEBULA_BUILD_EXAMPLES "Build example/demo executables under examples" ON)
option(NEBULA_ENABLE_DEBUG_SYMBOLS "Enable extra debug symbols for Debug builds" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)

if(NEBULA_ENABLE_DEBUG_SYMBOLS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -ggdb3 -fno-omit-frame-pointer)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic -Wl,--export-dynamic")
    message(STATUS "Debug build: full debug symbols enabled")
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/nebula/version.hpp.in
    ${CMAKE_CURRENT_SOURCE_DIR}/include/nebula/version.hpp
    @ONLY
)

set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/fmt)

find_package(Threads REQUIRED)

# ------------------------------------------------------------------
# Core exported targets (header-first layout with explicit module deps)
# ------------------------------------------------------------------
add_library(nebula_headers INTERFACE)
add_library(nebula::headers ALIAS nebula_headers)
set_target_properties(nebula_headers PROPERTIES EXPORT_NAME headers)
target_include_directories(nebula_headers
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(nebula_headers INTERFACE Threads::Threads)

add_library(nebula_containers INTERFACE)
add_library(nebula::containers ALIAS nebula_containers)
set_target_properties(nebula_containers PROPERTIES EXPORT_NAME containers)
target_link_libraries(nebula_containers INTERFACE nebula::headers)

add_library(nebula_config INTERFACE)
add_library(nebula::config ALIAS nebula_config)
set_target_properties(nebula_config PROPERTIES EXPORT_NAME config)
target_link_libraries(nebula_config INTERFACE nebula::headers)

add_library(nebula_memory INTERFACE)
add_library(nebula::memory ALIAS nebula_memory)
set_target_properties(nebula_memory PROPERTIES EXPORT_NAME memory)
target_link_libraries(nebula_memory INTERFACE nebula::headers)

add_library(nebula_threading INTERFACE)
add_library(nebula::threading ALIAS nebula_threading)
set_target_properties(nebula_threading PROPERTIES EXPORT_NAME threading)
target_link_libraries(nebula_threading INTERFACE nebula::headers)

add_library(nebula_logger INTERFACE)
add_library(nebula::logger ALIAS nebula_logger)
set_target_properties(nebula_logger PROPERTIES EXPORT_NAME logger)
target_link_libraries(nebula_logger INTERFACE nebula::containers nebula::config fmt::fmt)

add_library(nebula_io INTERFACE)
add_library(nebula::io ALIAS nebula_io)
set_target_properties(nebula_io PROPERTIES EXPORT_NAME io)
target_link_libraries(nebula_io INTERFACE nebula::containers nebula::threading nebula::logger)

add_library(nebula_all INTERFACE)
add_library(nebula::all ALIAS nebula_all)
set_target_properties(nebula_all PROPERTIES EXPORT_NAME all)
target_link_libraries(nebula_all INTERFACE
    nebula::containers
    nebula::config
    nebula::memory
    nebula::threading
    nebula::logger
    nebula::io
)

# Keep compatibility with Linux toolchains that still require explicit atomic.
target_link_libraries(nebula_headers INTERFACE $<$<NOT:$<PLATFORM_ID:Windows>>:atomic>)

function(nebula_add_unit_test target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE nebula::all)
endfunction()

function(nebula_add_example target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE nebula::all)
endfunction()

if(NEBULA_BUILD_TESTS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/CMakeLists.txt")
        add_subdirectory(tests/unit)
    else()
        message(STATUS "NEBULA_BUILD_TESTS is ON, but tests/unit is missing. Skipping unit tests.")
    endif()
endif()

if(NEBULA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

install(DIRECTORY include/ DESTINATION include)
install(TARGETS nebula_headers nebula_containers nebula_config nebula_memory nebula_threading nebula_logger nebula_io nebula_all
        EXPORT nebulaTargets)
install(EXPORT nebulaTargets NAMESPACE nebula:: DESTINATION lib/cmake/nebula-toolkit)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/nebula-toolkit-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/nebula-toolkit-config.cmake
    INSTALL_DESTINATION lib/cmake/nebula-toolkit
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/nebula-toolkit-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/nebula-toolkit-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/nebula-toolkit-config-version.cmake
    DESTINATION lib/cmake/nebula-toolkit
)
