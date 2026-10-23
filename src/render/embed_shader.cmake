# Writes <OUT>: a C++ include defining the SPIR-V / DXIL / MSL byte arrays of one shader and its oc::render::ShaderCode.
# Inputs: NAME (identifier), SPV, DXIL, MSL (file paths; DXIL / MSL may be empty or missing), OUT.
function(oc_hex_array var file out_size)
  set(bytes "0x00")
  set(size 0)
  if(file AND EXISTS "${file}")
    file(READ "${file}" hex HEX)
    string(LENGTH "${hex}" hex_length)
    math(EXPR size "${hex_length} / 2")
    if(size GREATER 0)
      string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    endif()
  endif()
  set(${var} "${bytes}" PARENT_SCOPE)
  set(${out_size} ${size} PARENT_SCOPE)
endfunction()

oc_hex_array(spirv_bytes "${SPV}" spirv_size)
oc_hex_array(dxil_bytes "${DXIL}" dxil_size)
oc_hex_array(msl_bytes "${MSL}" msl_size)

file(WRITE "${OUT}"
"static const unsigned char ${NAME}_spirv[] = {${spirv_bytes}};
static const unsigned char ${NAME}_dxil[] = {${dxil_bytes}};
static const unsigned char ${NAME}_msl[] = {${msl_bytes}};
static const ::oc::render::ShaderCode ${NAME}{${NAME}_spirv, ${spirv_size}, ${NAME}_dxil, ${dxil_size}, ${NAME}_msl, ${msl_size}};
")
