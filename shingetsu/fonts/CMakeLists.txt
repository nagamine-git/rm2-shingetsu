# Embeds the generated CJK glyphs (cjk32.bin) into the binary, like
# vendor/noto does for its font. Regenerate with gen_cjk_glyphs.py.
find_program(XXD xxd)

if("${XXD}" STREQUAL "XXD-NOTFOUND")
  message(FATAL_ERROR "XXD not found, required for the CJK glyphs")
  return()
endif()

set(BUILD_INCLUDE_DIR "${CMAKE_BINARY_DIR}/include")
set(GLYPH_FILE "cjk32.bin")
set(GLYPH_HEADER "${BUILD_INCLUDE_DIR}/${GLYPH_FILE}.h")
add_custom_command(
  OUTPUT "${GLYPH_HEADER}"
  DEPENDS "${GLYPH_FILE}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${BUILD_INCLUDE_DIR}"
  COMMAND ${XXD} -i "${GLYPH_FILE}" "${GLYPH_HEADER}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(ShingetsuFonts INTERFACE "${GLYPH_HEADER}")
target_include_directories(ShingetsuFonts INTERFACE "${BUILD_INCLUDE_DIR}")

add_library(Shingetsu::fonts ALIAS ShingetsuFonts)
