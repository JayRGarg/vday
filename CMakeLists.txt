cmake_minimum_required(VERSION 3.16)
project(valentine_tui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ftxui/CMakeLists.txt")
  message(FATAL_ERROR "FTXUI submodule missing. Run: git submodule update --init --recursive")
endif()

add_subdirectory(external/ftxui)

find_package(SDL2 QUIET)
find_package(SDL2_mixer QUIET)

add_executable(valentine_tui
  src/main.cpp
  src/app.cpp
  src/game.cpp
  src/audio.cpp
  src/persistence.cpp
)

target_include_directories(valentine_tui PRIVATE src)

if(SDL2_FOUND AND SDL2_mixer_FOUND)
  target_compile_definitions(valentine_tui PRIVATE HAVE_SDL2_MIXER=1)
  target_link_libraries(valentine_tui PRIVATE SDL2::SDL2 SDL2_mixer::SDL2_mixer)
endif()

target_link_libraries(valentine_tui PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
)

if(MSVC)
  target_compile_options(valentine_tui PRIVATE /W4)
else()
  target_compile_options(valentine_tui PRIVATE -Wall -Wextra -Wpedantic)
endif()
