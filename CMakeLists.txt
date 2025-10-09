cmake_minimum_required(VERSION 3.24)
project(swapchain_override_addon VERSION 1.0.0 LANGUAGES CXX)

# Allow normal variables to override options (needed for FetchContent dependencies)
cmake_policy(SET CMP0077 NEW)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check if ReShade submodule exists
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/reshade/include/reshade.hpp")
    message(FATAL_ERROR "ReShade submodule not found. Run: git submodule update --init --recursive")
endif()

# Use FetchContent for SafetyHook (it will automatically fetch Zydis as a dependency)
include(FetchContent)

# Configure SafetyHook to fetch Zydis and disable unnecessary builds
set(SAFETYHOOK_FETCH_ZYDIS ON)
set(SAFETYHOOK_BUILD_DOCS OFF)
set(SAFETYHOOK_BUILD_TEST OFF)
set(SAFETYHOOK_BUILD_EXAMPLES OFF)

# Configure Zydis options (applied when SafetyHook fetches it)
set(ZYDIS_BUILD_EXAMPLES OFF)
set(ZYDIS_BUILD_TOOLS OFF)
set(ZYDIS_BUILD_DOXYGEN OFF)

message(STATUS "Fetching SafetyHook...")
FetchContent_Declare(safetyhook
    GIT_REPOSITORY "https://github.com/cursey/safetyhook"
    GIT_TAG "main"
)
FetchContent_MakeAvailable(safetyhook)

# Find fxc.exe for shader compilation
find_program(FXC_EXECUTABLE fxc
    PATHS
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x86"
    NO_DEFAULT_PATH
)

if(NOT FXC_EXECUTABLE)
    message(WARNING "fxc.exe not found. Shaders will not be compiled. Please compile manually or install Windows SDK.")
endif()

# Function to compile HLSL shaders
function(compile_shader SHADER_FILE SHADER_TYPE ENTRY_POINT OUTPUT_FILE)
    if(FXC_EXECUTABLE)
        add_custom_command(
            OUTPUT ${OUTPUT_FILE}
            COMMAND ${FXC_EXECUTABLE} /T ${SHADER_TYPE} /E ${ENTRY_POINT} /Fo ${OUTPUT_FILE} ${SHADER_FILE}
            DEPENDS ${SHADER_FILE}
            COMMENT "Compiling shader: ${SHADER_FILE}"
            VERBATIM
        )
    else()
        # Create a dummy target if FXC is not available
        add_custom_command(
            OUTPUT ${OUTPUT_FILE}
            COMMAND ${CMAKE_COMMAND} -E echo "Warning: Shader ${SHADER_FILE} not compiled (fxc.exe not found)"
            COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT_FILE}
            DEPENDS ${SHADER_FILE}
        )
    endif()
endfunction()

# Compile shaders
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

compile_shader(
    "${CMAKE_SOURCE_DIR}/shaders/fullscreen_vs.hlsl"
    "vs_4_0"
    "main"
    "${SHADER_OUTPUT_DIR}/fullscreen_vs.cso"
)

compile_shader(
    "${CMAKE_SOURCE_DIR}/shaders/copy_ps.hlsl"
    "ps_4_0"
    "main"
    "${SHADER_OUTPUT_DIR}/copy_ps.cso"
)

# Generate header file with shader bytecode
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/generated/shader_bytecode.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated"
    COMMAND ${CMAKE_COMMAND}
        -DSHADER_OUTPUT_DIR="${SHADER_OUTPUT_DIR}"
        -DOUTPUT_FILE="${CMAKE_BINARY_DIR}/generated/shader_bytecode.h"
        -P "${CMAKE_SOURCE_DIR}/cmake/GenerateShaderHeader.cmake"
    DEPENDS
        "${SHADER_OUTPUT_DIR}/fullscreen_vs.cso"
        "${SHADER_OUTPUT_DIR}/copy_ps.cso"
    COMMENT "Generating shader bytecode header"
)

# Collect source files
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Create shared library
add_library(${PROJECT_NAME} SHARED
    ${SOURCES}
    "${CMAKE_BINARY_DIR}/generated/shader_bytecode.h"
)

# Set output name
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "swapchain_override_addon"
    PREFIX ""
    SUFFIX ".dll"
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/reshade/include
    ${CMAKE_SOURCE_DIR}/external/reshade/deps/imgui
    ${CMAKE_BINARY_DIR}/generated
)

# Link SafetyHook
target_link_libraries(${PROJECT_NAME} PRIVATE safetyhook)

# Compiler-specific flags
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4                 # Warning level 4
        /WX-                # Don't treat warnings as errors
        /permissive-        # Standards conformance
        /Zc:__cplusplus     # Enable updated __cplusplus macro
    )
    # Disable specific warnings
    target_compile_options(${PROJECT_NAME} PRIVATE
        /wd4100             # Unreferenced formal parameter
        /wd4505             # Unreferenced local function
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
    )
endif()

# Windows-specific settings
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# Architecture-specific output suffix
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "swapchain_override_addon64")
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "swapchain_override_addon32")
endif()

# Post-build: Copy and rename to .addon
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:${PROJECT_NAME}>
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/$<TARGET_FILE_BASE_NAME:${PROJECT_NAME}>.addon
    COMMENT "Creating .addon file"
)

# Installation
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)

# Install .addon file as well
install(FILES $<TARGET_FILE_DIR:${PROJECT_NAME}>/$<TARGET_FILE_BASE_NAME:${PROJECT_NAME}>.addon
    DESTINATION bin
)
