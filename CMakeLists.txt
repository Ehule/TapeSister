cmake_minimum_required(VERSION 3.16)
project(TapeSister VERSION 0.9.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(tapesister_core src/ts_sample.c src/ts_fm.c src/ts_audition.c src/ts_note_bank.c
             src/ts_note_event.c src/ts_midi_map.c src/ts_performance.c src/ts_audio_mixer.c
             src/ts_realtime_diagnostics.c
             src/ts_sister_effects.c src/ts_sister_fallout.c src/ts_sister_post_fx.c
             src/ts_sister_limiter.c
             src/ts_sister_machine.c src/ts_sister_wave_snapshot.c
             src/ts_sister_runtime.c src/ts_sister_ui.c
             src/ts_sister_preset.c src/ts_sister_project_state.c
             src/ts_capture.c
             src/ts_capture_archive.c src/ts_performance_recorder.c
             src/ts_input_monitor.c
             src/ts_input_ownership.c
             src/ts_sample_pages.c
             src/ts_browser.c src/ts_config.c src/ts_audio_config.c
             src/ts_recipe.c src/ts_dsp_recipe.c
             src/ts_palette.c
             src/ts_render_damage.c src/ts_waveform_cache.c
             src/ts_cdp_recipe.c src/ts_cdp_adapter.c src/ts_transform.c
             src/ts_dsp_transform.c src/ts_exchange.c src/ts_waveform_display.c
             src/ts_ui.c)
target_include_directories(tapesister_core PUBLIC include)
target_link_libraries(tapesister_core PUBLIC m)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(tapesister_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(TAPESISTER_BUILD_SDL "Build the playable SDL2 instrument" ON)
option(TAPESISTER_ENABLE_MIDI "Enable RtMidi note input" ON)
option(TAPESISTER_BUNDLE_CDP8
       "Build and stage the pinned CDP8 runtime beside TapeSister" ON)
if(TAPESISTER_BUNDLE_CDP8)
  include(cmake/CDP8Runtime.cmake)
endif()
if(TAPESISTER_BUILD_SDL)
  find_package(SDL2 REQUIRED)
  # main_sdl.c is the one canonical callback/application translation unit.
  add_executable(tapesister src/main_sdl.c src/ts_startup_diag.c
                            src/ts_midi_input.c)
  target_include_directories(tapesister PRIVATE third_party)
  target_link_libraries(tapesister PRIVATE tapesister_core SDL2::SDL2)
  if(WIN32)
    target_compile_definitions(tapesister PRIVATE SDL_MAIN_HANDLED)
  endif()
  set(TAPESISTER_MIDI_AVAILABLE OFF)
  if(TAPESISTER_ENABLE_MIDI)
    if(WIN32 OR APPLE)
      set(TAPESISTER_MIDI_AVAILABLE ON)
    elseif(UNIX)
      find_package(ALSA QUIET)
      if(ALSA_FOUND)
        set(TAPESISTER_MIDI_AVAILABLE ON)
      else()
        message(WARNING "ALSA development files not found; building without MIDI input")
      endif()
    endif()
  endif()
  if(TAPESISTER_MIDI_AVAILABLE)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    target_sources(tapesister PRIVATE
      third_party/rtmidi/RtMidi.cpp
      third_party/rtmidi/rtmidi_c.cpp)
    target_compile_definitions(tapesister PRIVATE TAPESISTER_HAS_MIDI)
    if(WIN32)
      target_compile_definitions(tapesister PRIVATE __WINDOWS_MM__)
      target_link_libraries(tapesister PRIVATE winmm)
    elseif(APPLE)
      find_library(COREMIDI CoreMIDI REQUIRED)
      find_library(COREFOUNDATION CoreFoundation REQUIRED)
      target_compile_definitions(tapesister PRIVATE __MACOSX_CORE__)
      target_link_libraries(tapesister PRIVATE ${COREMIDI} ${COREFOUNDATION})
    else()
      find_package(Threads REQUIRED)
      target_compile_definitions(tapesister PRIVATE __LINUX_ALSA__)
      target_link_libraries(tapesister PRIVATE ALSA::ALSA Threads::Threads dl)
    endif()
  endif()
  # Keep the enlarged Windows stack reserve as defense-in-depth after PR35's
  # REC-bank storage was moved off the main-thread stack.
  if(MINGW)
    target_link_options(tapesister PRIVATE "-Wl,--stack,16777216")

    # Explorer does not inherit MSYS2's /ucrt64/bin PATH.  Keep the build-tree
    # executable portable by placing the matching SDL and MinGW runtime DLLs
    # beside it after every link.  In particular, RtMidi enables C++ and adds
    # libstdc++ to an application that was previously C-only.
    set(TAPESISTER_MINGW_RUNTIME_COMPILER "${CMAKE_C_COMPILER}")
    if(TAPESISTER_MIDI_AVAILABLE)
      set(TAPESISTER_MINGW_RUNTIME_COMPILER "${CMAKE_CXX_COMPILER}")
    endif()
    get_filename_component(TAPESISTER_MINGW_BIN_DIR
                           "${TAPESISTER_MINGW_RUNTIME_COMPILER}" DIRECTORY)

    function(tapesister_resolve_mingw_dll output_variable dll_name)
      execute_process(
        COMMAND "${TAPESISTER_MINGW_RUNTIME_COMPILER}"
                "-print-file-name=${dll_name}"
        OUTPUT_VARIABLE resolved_dll
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
      if(NOT IS_ABSOLUTE "${resolved_dll}" OR
         NOT EXISTS "${resolved_dll}")
        unset(resolved_dll)
        unset(resolved_dll CACHE)
        find_file(resolved_dll
          NAMES "${dll_name}"
          HINTS
            "${TAPESISTER_MINGW_BIN_DIR}"
            "$ENV{MINGW_PREFIX}/bin"
            "$ENV{MSYSTEM_PREFIX}/bin"
          NO_DEFAULT_PATH)
      endif()
      if(EXISTS "${resolved_dll}")
        set(${output_variable} "${resolved_dll}" PARENT_SCOPE)
      else()
        set(${output_variable} "" PARENT_SCOPE)
      endif()
    endfunction()

    set(TAPESISTER_WINDOWS_RUNTIME_DLL_NAMES SDL2.dll)
    if(TAPESISTER_MIDI_AVAILABLE)
      list(APPEND TAPESISTER_WINDOWS_RUNTIME_DLL_NAMES
        libstdc++-6.dll
        libwinpthread-1.dll)
    endif()
    list(APPEND TAPESISTER_WINDOWS_RUNTIME_DLL_NAMES
      libgcc_s_seh-1.dll
      libgcc_s_sjlj-1.dll
      libgcc_s_dw2-1.dll)
    set(TAPESISTER_WINDOWS_RUNTIME_DLLS "")
    set(TAPESISTER_MINGW_GCC_RUNTIME_FOUND OFF)
    foreach(runtime_name IN LISTS TAPESISTER_WINDOWS_RUNTIME_DLL_NAMES)
      tapesister_resolve_mingw_dll(runtime_path "${runtime_name}")
      if(runtime_path)
        list(APPEND TAPESISTER_WINDOWS_RUNTIME_DLLS "${runtime_path}")
        if(runtime_name MATCHES "^libgcc_s_.*\\.dll$")
          set(TAPESISTER_MINGW_GCC_RUNTIME_FOUND ON)
        endif()
      elseif(runtime_name STREQUAL "SDL2.dll" OR
             (TAPESISTER_MIDI_AVAILABLE AND
              (runtime_name STREQUAL "libstdc++-6.dll" OR
               runtime_name STREQUAL "libwinpthread-1.dll")))
        message(FATAL_ERROR
          "Required Windows runtime ${runtime_name} was not found beside "
          "the MinGW compiler (${TAPESISTER_MINGW_BIN_DIR})")
      endif()
    endforeach()
    if(NOT TAPESISTER_MINGW_GCC_RUNTIME_FOUND)
      message(FATAL_ERROR
        "No supported MinGW libgcc runtime DLL was found beside the compiler")
    endif()
    list(REMOVE_DUPLICATES TAPESISTER_WINDOWS_RUNTIME_DLLS)
    foreach(runtime_dll IN LISTS TAPESISTER_WINDOWS_RUNTIME_DLLS)
      add_custom_command(TARGET tapesister POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${runtime_dll}" "$<TARGET_FILE_DIR:tapesister>"
        VERBATIM)
    endforeach()
  endif()
  add_custom_command(TARGET tapesister POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:tapesister>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/tapesister_splash.png"
            "$<TARGET_FILE_DIR:tapesister>/assets/tapesister_splash.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/palette.pal"
            "$<TARGET_FILE_DIR:tapesister>/assets/palette.pal"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/tapesister.ini.example"
            "$<TARGET_FILE_DIR:tapesister>/tapesister.ini.example"
    COMMENT "Copying TapeSister runtime artwork and palette")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/tapesister_welcome.wav")
    add_custom_command(TARGET tapesister POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/tapesister_welcome.wav"
              "$<TARGET_FILE_DIR:tapesister>/assets/tapesister_welcome.wav"
      COMMENT "Copying TapeSister welcome sample")
  endif()
  if(TAPESISTER_BUNDLE_CDP8)
    tapesister_add_cdp8_runtime(tapesister)
  endif()
endif()

enable_testing()
add_executable(tapesister_core_tests tests/test_core.c)
target_link_libraries(tapesister_core_tests PRIVATE tapesister_core)
add_test(NAME tapesister_core_tests COMMAND tapesister_core_tests)

add_executable(tapesister_audio_frame_tests tests/test_audio_frame.c)
target_link_libraries(tapesister_audio_frame_tests PRIVATE tapesister_core)
add_test(NAME tapesister_audio_frame_tests COMMAND tapesister_audio_frame_tests)

if(TAPESISTER_BUILD_SDL AND TAPESISTER_BUNDLE_CDP8)
  add_executable(tapesister_cdp_native_smoke tests/test_cdp_native_smoke.c)
  target_link_libraries(tapesister_cdp_native_smoke PRIVATE tapesister_core)
  add_dependencies(tapesister_cdp_native_smoke tapesister)
  add_test(NAME tapesister_cdp_native_smoke
    COMMAND tapesister_cdp_native_smoke
            "$<TARGET_FILE_DIR:tapesister>/cdp/bin"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/tapesister_welcome.wav")
endif()

foreach(stereo_test IN ITEMS audio_mixer note_bank_stereo performance_stereo
                             capture_stereo performance_recorder
                             external_input_channels
                             input_ownership realtime_diagnostics)
  add_executable(test_${stereo_test} tests/test_${stereo_test}.c)
  target_link_libraries(test_${stereo_test} PRIVATE tapesister_core)
  add_test(NAME test_${stereo_test} COMMAND test_${stereo_test})
endforeach()

foreach(sister_test IN ITEMS sister_buffer sister_resize sister_heads sister_transport
                             sister_modulation sister_feedback sister_stereo
                             sister_weave
                             sister_effect_routing
                             sister_post_fx sister_fallout sister_limiter
                             sister_duck_filter sister_snapshot
                             sister_routes
                             sister_runtime sister_source_mask sister_capture
                             sister_performance_sources sister_recursion
                             sister_lifecycle sister_visibility sister_ui_model
                             sister_wave_snapshot waveform_display_modes
                             sister_source_ui sister_capture_ui sister_palette
                             sister_ghost_tone sister_preset
                             sister_project_state sister_pathological)
  add_executable(test_${sister_test} tests/test_${sister_test}.c)
  target_link_libraries(test_${sister_test} PRIVATE tapesister_core)
  add_test(NAME test_${sister_test} COMMAND test_${sister_test})
endforeach()

add_custom_target(stress-sister
  COMMAND test_sister_pathological --certify
  DEPENDS test_sister_pathological
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Running deterministic two-hour Sister certification soak")

add_executable(benchmark_sister_callback EXCLUDE_FROM_ALL
               tests/benchmark_sister_callback.c)
target_link_libraries(benchmark_sister_callback PRIVATE tapesister_core)
add_custom_target(benchmark-sister
  COMMAND benchmark_sister_callback 2000000 256
  DEPENDS benchmark_sister_callback
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Running optional Sister callback benchmark")

add_executable(tapesister_sample_channels_tests tests/test_sample_channels.c)
target_link_libraries(tapesister_sample_channels_tests PRIVATE tapesister_core)
add_test(NAME tapesister_sample_channels_tests COMMAND tapesister_sample_channels_tests)

add_executable(tapesister_tsr27_tests tests/test_tsr27.c)
target_link_libraries(tapesister_tsr27_tests PRIVATE tapesister_core)
add_test(NAME tapesister_tsr27_tests COMMAND tapesister_tsr27_tests)

add_executable(tapesister_wav_channels_tests tests/test_wav_channels.c)
target_link_libraries(tapesister_wav_channels_tests PRIVATE tapesister_core)
add_test(NAME tapesister_wav_channels_tests COMMAND tapesister_wav_channels_tests)

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

add_executable(tapesister_performance_tests tests/test_performance.c)
target_link_libraries(tapesister_performance_tests PRIVATE tapesister_core)
add_test(NAME tapesister_performance_tests COMMAND tapesister_performance_tests)

add_executable(tapesister_midi_tests tests/test_midi.c src/ts_midi_input.c)
target_link_libraries(tapesister_midi_tests PRIVATE tapesister_core)
add_test(NAME tapesister_midi_tests COMMAND tapesister_midi_tests)

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
