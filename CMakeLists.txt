cmake_minimum_required(VERSION 3.16)
project(TapeSister VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(tapesister_core src/ts_sample.c src/ts_audition.c src/ts_note_bank.c
             src/ts_browser.c src/ts_config.c src/ts_recipe.c src/ts_ui.c)
target_include_directories(tapesister_core PUBLIC include)
target_link_libraries(tapesister_core PUBLIC m)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(tapesister_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(TAPESISTER_BUILD_SDL "Build the playable SDL2 instrument" ON)
if(TAPESISTER_BUILD_SDL)
  find_package(SDL2 REQUIRED)
  add_executable(tapesister src/main_sdl.c)
  target_include_directories(tapesister PRIVATE third_party)
  target_link_libraries(tapesister PRIVATE tapesister_core SDL2::SDL2)
  add_custom_command(TARGET tapesister POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:tapesister>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/tapesister_splash.png"
            "$<TARGET_FILE_DIR:tapesister>/assets/tapesister_splash.png"
    COMMENT "Copying TapeSister splash artwork")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/tapesister_welcome.wav")
    add_custom_command(TARGET tapesister POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/tapesister_welcome.wav"
              "$<TARGET_FILE_DIR:tapesister>/assets/tapesister_welcome.wav"
      COMMENT "Copying TapeSister welcome sample")
  endif()
endif()

enable_testing()
add_executable(tapesister_core_tests tests/test_core.c)
target_link_libraries(tapesister_core_tests PRIVATE tapesister_core)
add_test(NAME tapesister_core_tests COMMAND tapesister_core_tests)

add_executable(tapesister_smear_tests tests/test_smear.c)
target_link_libraries(tapesister_smear_tests PRIVATE tapesister_core)
add_test(NAME tapesister_smear_tests COMMAND tapesister_smear_tests)

add_executable(tapesister_tear_tests tests/test_tear.c)
target_link_libraries(tapesister_tear_tests PRIVATE tapesister_core)
add_test(NAME tapesister_tear_tests COMMAND tapesister_tear_tests)

add_executable(tapesister_bank_tests tests/test_bank.c)
target_link_libraries(tapesister_bank_tests PRIVATE tapesister_core)
add_test(NAME tapesister_bank_tests COMMAND tapesister_bank_tests)

add_executable(tapesister_render_demo tests/render_demo.c)
target_link_libraries(tapesister_render_demo PRIVATE tapesister_core)
