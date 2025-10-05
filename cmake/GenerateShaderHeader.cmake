# Script to generate a C++ header file containing shader bytecode as static arrays

file(READ "${SHADER_OUTPUT_DIR}/fullscreen_vs.cso" VS_BYTECODE_HEX HEX)
file(READ "${SHADER_OUTPUT_DIR}/copy_ps.cso" PS_BYTECODE_HEX HEX)

# Convert hex string to C array format
string(REGEX MATCHALL "([A-Fa-f0-9][A-Fa-f0-9])" VS_BYTECODE_LIST "${VS_BYTECODE_HEX}")
string(REGEX MATCHALL "([A-Fa-f0-9][A-Fa-f0-9])" PS_BYTECODE_LIST "${PS_BYTECODE_HEX}")

set(VS_ARRAY "")
set(PS_ARRAY "")

foreach(BYTE ${VS_BYTECODE_LIST})
    string(APPEND VS_ARRAY "0x${BYTE}, ")
endforeach()

foreach(BYTE ${PS_BYTECODE_LIST})
    string(APPEND PS_ARRAY "0x${BYTE}, ")
endforeach()

# Get array sizes
list(LENGTH VS_BYTECODE_LIST VS_SIZE)
list(LENGTH PS_BYTECODE_LIST PS_SIZE)

# Generate header file
file(WRITE "${OUTPUT_FILE}" "// Auto-generated file - do not edit manually\n")
file(APPEND "${OUTPUT_FILE}" "// Generated from shader bytecode\n\n")
file(APPEND "${OUTPUT_FILE}" "#pragma once\n\n")
file(APPEND "${OUTPUT_FILE}" "#include <cstdint>\n\n")
file(APPEND "${OUTPUT_FILE}" "namespace shader_bytecode {\n\n")

file(APPEND "${OUTPUT_FILE}" "// Fullscreen vertex shader (Shader Model 4.0)\n")
file(APPEND "${OUTPUT_FILE}" "constexpr uint8_t fullscreen_vs[] = {\n    ${VS_ARRAY}\n};\n")
file(APPEND "${OUTPUT_FILE}" "constexpr size_t fullscreen_vs_size = ${VS_SIZE};\n\n")

file(APPEND "${OUTPUT_FILE}" "// Copy pixel shader (Shader Model 4.0)\n")
file(APPEND "${OUTPUT_FILE}" "constexpr uint8_t copy_ps[] = {\n    ${PS_ARRAY}\n};\n")
file(APPEND "${OUTPUT_FILE}" "constexpr size_t copy_ps_size = ${PS_SIZE};\n\n")

file(APPEND "${OUTPUT_FILE}" "} // namespace shader_bytecode\n")

message(STATUS "Generated shader bytecode header: ${OUTPUT_FILE}")
message(STATUS "  - Vertex shader size: ${VS_SIZE} bytes")
message(STATUS "  - Pixel shader size: ${PS_SIZE} bytes")
