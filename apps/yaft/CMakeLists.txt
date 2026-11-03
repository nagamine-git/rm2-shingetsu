project(yaft)

# Embedded data for the Japanese input: the kana tables and CJK glyphs. These
# live outside of apps/ since they are not specific to the code using them.
add_subdirectory(${CMAKE_SOURCE_DIR}/shingetsu/tables
                 ${CMAKE_CURRENT_BINARY_DIR}/shingetsu-tables)
add_subdirectory(${CMAKE_SOURCE_DIR}/shingetsu/fonts
                 ${CMAKE_CURRENT_BINARY_DIR}/shingetsu-fonts)

# Input method logic without any UI or framebuffer dependency.
add_library(
  yaft_ime_lib STATIC
  ime/Composer.cpp ime/ImeController.cpp ime/MozcClient.cpp ime/MozcServer.cpp
  ime/MozcProtocol.cpp ime/Sexp.cpp)
target_include_directories(yaft_ime_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(yaft_ime_lib PUBLIC Shingetsu::tables)

add_library(
  yaft_app_lib OBJECT
  YaftWidget.cpp
  keyboard.cpp
  screen.cpp
  layout.cpp
  keymap.cpp
  config.cpp
  terminal_adapter.cpp
  ime/ImeBar.cpp)
add_library(Yaft::app_lib ALIAS yaft_app_lib)

target_include_directories(yaft_app_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(
  yaft_app_lib PUBLIC rMlib Ghostty::vt util tomlplusplus::tomlplusplus
                      yaft_ime_lib Shingetsu::fonts)

if(APPLE)
  target_link_libraries(yaft_app_lib PUBLIC linux::mxcfb)
endif()

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE yaft_app_lib rMlib)

add_executable(yaft_reader reader.cpp)
target_link_libraries(yaft_reader PRIVATE yaft_app_lib rMlib)

# Headless driver for the input method, for tests and debugging.
add_executable(yaft-ime-cli ime/cli.cpp)
target_link_libraries(yaft-ime-cli PRIVATE yaft_ime_lib)

install(
  TARGETS ${PROJECT_NAME}
  COMPONENT ${PROJECT_NAME}
  DESTINATION ${OPT_PREFIX}bin)

install(
  TARGETS yaft_reader
  COMPONENT ${PROJECT_NAME}
  DESTINATION ${OPT_PREFIX}bin)

install(
  TARGETS yaft-ime-cli
  COMPONENT ${PROJECT_NAME}
  DESTINATION ${OPT_PREFIX}bin)

install(
  FILES draft/yaft.draft
  COMPONENT ${PROJECT_NAME}
  DESTINATION ${OPT_PREFIX}etc/draft)
install(
  FILES draft/yaft.png
  COMPONENT ${PROJECT_NAME}
  DESTINATION ${OPT_PREFIX}etc/draft/icons)

add_deploy(${PROJECT_NAME})
