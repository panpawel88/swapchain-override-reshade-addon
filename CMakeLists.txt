cmake_minimum_required(VERSION 3.20)
project(swapchain_override_addon VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check if ReShade submodule exists
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/reshade/include/reshade.hpp")
    message(FATAL_ERROR "ReShade submodule not found. Run: git submodule update --init --recursive")
endif()

# Collect source files
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Create shared library
add_library(${PROJECT_NAME} SHARED ${SOURCES})

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
)

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
