# Embeds the hazkey input tables into the binary (same approach as
# vendor/noto, which generates a header with xxd -i).
find_program(XXD xxd)

if("${XXD}" STREQUAL "XXD-NOTFOUND")
  message(FATAL_ERROR "XXD not found, required for the input tables")
  return()
endif()

set(BUILD_INCLUDE_DIR "${CMAKE_BINARY_DIR}/include")
set(TABLE_FILES shingetsu-ansi-colemak.tsv shingetsu-ansi-qwerty.tsv)

set(TABLE_HEADERS "")
foreach(table ${TABLE_FILES})
  set(header "${BUILD_INCLUDE_DIR}/${table}.h")
  add_custom_command(
    OUTPUT "${header}"
    DEPENDS "${table}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BUILD_INCLUDE_DIR}"
    COMMAND ${XXD} -i "${table}" "${header}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  list(APPEND TABLE_HEADERS "${header}")
endforeach()

add_library(ShingetsuTables INTERFACE ${TABLE_HEADERS})
target_include_directories(ShingetsuTables INTERFACE "${BUILD_INCLUDE_DIR}")

add_library(Shingetsu::tables ALIAS ShingetsuTables)
