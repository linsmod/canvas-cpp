if(NOT DEFINED SHIM_INPUT OR NOT DEFINED SHIM_OUTPUT)
    message(FATAL_ERROR "GenerateShim.cmake requires SHIM_INPUT and SHIM_OUTPUT")
endif()

file(READ "${SHIM_INPUT}" _shim_hex HEX)
string(LENGTH "${_shim_hex}" _hex_length)
math(EXPR _byte_count "${_hex_length} / 2")

if(_byte_count GREATER 0)
    string(REGEX REPLACE "(..)" "0x\\1," _shim_bytes "${_shim_hex}")
else()
    set(_shim_bytes "")
endif()

get_filename_component(_output_dir "${SHIM_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")

file(WRITE "${SHIM_OUTPUT}"
"// Generated from browser_shim.js - do not edit.\n#include \"browser_shim.h\"\n\nconst unsigned char BROWSER_SHIM_JS[] = { ${_shim_bytes} 0 };\nconst unsigned int BROWSER_SHIM_JS_LEN = ${_byte_count};\n")
