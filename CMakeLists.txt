cmake_minimum_required(VERSION 3.16)
project(TapeSister VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(tapesister_core src/ts_sample.c src/ts_audition.c src/ts_note_bank.c
             src/ts_capture.c src/ts_capture_archive.c src/ts_input_monitor.c
             src/ts_sample_pages.c
             src/ts_browser.c src/ts_config.c src/ts_audio_config.c
             src/ts_recipe.c src/ts_dsp_recipe.c
             src/ts_palette.c
             src/ts_render_damage.c src/ts_waveform_cache.c
             src/ts_cdp_recipe.c src/ts_cdp_adapter.c src/ts_transform.c
             src/ts_dsp_transform.c src/ts_exchange.c src/ts_ui.c)
target_include_directories(tapesister_core PUBLIC include)
target_link_libraries(tapesister_core PUBLIC m)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(tapesister_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(TAPESISTER_BUILD_SDL "Build the playable SDL2 instrument" ON)
if(TAPESISTER_BUILD_SDL)
  find_package(SDL2 REQUIRED)
  # main_sdl_audio.c wraps the app entry point with SDL device selection and
  # routing; the runtime implementation remains in main_sdl.c.
  add_executable(tapesister src/main_sdl_audio.c src/ts_startup_diag.c)
  target_include_directories(tapesister PRIVATE third_party)
  target_link_libraries(tapesister PRIVATE tapesister_core SDL2::SDL2)
  # Keep the enlarged Windows stack reserve as defense-in-depth after PR35's
  # REC-bank storage was moved off the main-thread stack.
  if(MINGW)
    target_link_options(tapesister PRIVATE "-Wl,--stack,16777216")
  endif()
  add_custom_command(TARGET tapesister POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:tapesister>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/tapesister_splash.png"
            "$<TARGET_FILE_DIR:tapesister>/assets/tapesister_splash.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/tapehead.pal"
            "$<TARGET_FILE_DIR:tapesister>/assets/tapehead.pal"
    COMMENT "Copying TapeSister runtime artwork and palette")
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

add_executable(tapesister_editor_contract_tests tests/test_editor_contract.c)
target_link_libraries(tapesister_editor_contract_tests PRIVATE tapesister_core)
add_test(NAME tapesister_editor_contract_tests COMMAND tapesister_editor_contract_tests)

add_executable(tapesister_canvas_tests tests/test_canvas.c)
target_link_libraries(tapesister_canvas_tests PRIVATE tapesister_core)
add_test(NAME tapesister_canvas_tests COMMAND tapesister_canvas_tests)

add_executable(tapesister_capture_tests tests/test_capture.c)
target_link_libraries(tapesister_capture_tests PRIVATE tapesister_core)
add_test(NAME tapesister_capture_tests COMMAND tapesister_capture_tests)

add_executable(tapesister_external_record_tests tests/test_external_record.c)
target_link_libraries(tapesister_external_record_tests PRIVATE tapesister_core)
add_test(NAME tapesister_external_record_tests COMMAND tapesister_external_record_tests)

add_executable(tapesister_input_monitor_tests tests/test_input_monitor.c)
target_link_libraries(tapesister_input_monitor_tests PRIVATE tapesister_core)
add_test(NAME tapesister_input_monitor_tests COMMAND tapesister_input_monitor_tests)

add_executable(tapesister_capture_archive_tests tests/test_capture_archive.c)
target_link_libraries(tapesister_capture_archive_tests PRIVATE tapesister_core)
add_test(NAME tapesister_capture_archive_tests COMMAND tapesister_capture_archive_tests)

add_executable(tapesister_sample_pages_tests tests/test_sample_pages.c)
target_link_libraries(tapesister_sample_pages_tests PRIVATE tapesister_core)
add_test(NAME tapesister_sample_pages_tests COMMAND tapesister_sample_pages_tests)

add_executable(tapesister_audio_config_tests tests/test_audio_config.c)
target_link_libraries(tapesister_audio_config_tests PRIVATE tapesister_core)
add_test(NAME tapesister_audio_config_tests COMMAND tapesister_audio_config_tests)

add_executable(tapesister_transform_tests tests/test_transform.c)
target_link_libraries(tapesister_transform_tests PRIVATE tapesister_core)
add_test(NAME tapesister_transform_tests COMMAND tapesister_transform_tests)

add_executable(tapesister_chain_stamp_tests tests/test_chain_stamp.c)
target_link_libraries(tapesister_chain_stamp_tests PRIVATE tapesister_core)
add_test(NAME tapesister_chain_stamp_tests COMMAND tapesister_chain_stamp_tests)

add_executable(tapesister_exchange_tests tests/test_exchange.c)
target_link_libraries(tapesister_exchange_tests PRIVATE tapesister_core)
add_test(NAME tapesister_exchange_tests COMMAND tapesister_exchange_tests)

add_executable(tapesister_render_demo tests/render_demo.c)
target_link_libraries(tapesister_render_demo PRIVATE tapesister_core)

add_executable(tapesister_render_damage_tests tests/test_render_damage.c)
target_link_libraries(tapesister_render_damage_tests PRIVATE tapesister_core)
add_test(NAME tapesister_render_damage_tests COMMAND tapesister_render_damage_tests)

add_executable(tapesister_waveform_cache_tests tests/test_waveform_cache.c)
target_link_libraries(tapesister_waveform_cache_tests PRIVATE tapesister_core)
add_test(NAME tapesister_waveform_cache_tests COMMAND tapesister_waveform_cache_tests)

add_executable(tapesister_render_efficiency_tests tests/test_render_efficiency.c)
target_link_libraries(tapesister_render_efficiency_tests PRIVATE tapesister_core)
add_test(NAME tapesister_render_efficiency_tests COMMAND tapesister_render_efficiency_tests)
