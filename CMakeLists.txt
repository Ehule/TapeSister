cmake_minimum_required(VERSION 3.16)
project(TapeSister VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(tapesister_core src/ts_sample.c src/ts_audition.c src/ts_note_bank.c
             src/ts_browser.c src/ts_ui.c)
target_include_directories(tapesister_core PUBLIC include)
target_link_libraries(tapesister_core PUBLIC m)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(tapesister_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(TAPESISTER_BUILD_SDL "Build the playable SDL2 instrument" ON)
if(TAPESISTER_BUILD_SDL)
  find_package(SDL2 REQUIRED)
  add_executable(tapesister src/main_sdl.c)
  target_link_libraries(tapesister PRIVATE tapesister_core SDL2::SDL2)
endif()

enable_testing()
add_executable(tapesister_core_tests tests/test_core.c)
target_link_libraries(tapesister_core_tests PRIVATE tapesister_core)
add_test(NAME tapesister_core_tests COMMAND tapesister_core_tests)

add_executable(tapesister_render_demo tests/render_demo.c)
target_link_libraries(tapesister_render_demo PRIVATE tapesister_core)
