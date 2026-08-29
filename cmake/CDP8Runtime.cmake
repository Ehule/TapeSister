include_guard(GLOBAL)
include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/CDP8Manifest.cmake")
set(TAPESISTER_CDP8_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

set(TAPESISTER_CDP8_SOURCE_DIR "" CACHE PATH
    "Existing CDP8 source checkout; empty fetches the pinned upstream commit")
set(TAPESISTER_CDP8_SOURCE_ARCHIVE "" CACHE FILEPATH
    "Exact CDP8 source archive to ship when SOURCE_DIR is not a Git checkout")

function(tapesister_add_cdp8_runtime application_target)
  if(NOT TARGET "${application_target}")
    message(FATAL_ERROR "Unknown TapeSister application target: ${application_target}")
  endif()

  set(cdp8_prefix "${CMAKE_CURRENT_BINARY_DIR}/_deps/cdp8")
  set(cdp8_binary_dir "${cdp8_prefix}/build")
  set(cdp8_cmake_args
      "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
      "-DBUILD_RELEASE=ON"
      "-DUSE_LOCAL_PORTAUDIO=OFF")
  if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND cdp8_cmake_args
         "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
  endif()

  if(TAPESISTER_CDP8_SOURCE_DIR)
    get_filename_component(cdp8_source_dir
                           "${TAPESISTER_CDP8_SOURCE_DIR}" ABSOLUTE)
    if(NOT EXISTS "${cdp8_source_dir}/CMakeLists.txt" OR
       NOT EXISTS "${cdp8_source_dir}/LICENSE")
      message(FATAL_ERROR
        "TAPESISTER_CDP8_SOURCE_DIR is not a complete CDP8 source tree: "
        "${cdp8_source_dir}")
    endif()
    ExternalProject_Add(tapesister_cdp8_runtime
      PREFIX "${cdp8_prefix}"
      SOURCE_DIR "${cdp8_source_dir}"
      BINARY_DIR "${cdp8_binary_dir}"
      DOWNLOAD_COMMAND ""
      UPDATE_COMMAND ""
      CMAKE_ARGS ${cdp8_cmake_args}
      BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR>
                    --config $<CONFIG>
                    --target ${TAPESISTER_CDP8_REQUIRED_PROGRAMS}
      INSTALL_COMMAND ""
      USES_TERMINAL_CONFIGURE TRUE
      USES_TERMINAL_BUILD TRUE)
  else()
    set(cdp8_source_dir "${cdp8_prefix}/src/tapesister_cdp8_runtime")
    ExternalProject_Add(tapesister_cdp8_runtime
      PREFIX "${cdp8_prefix}"
      GIT_REPOSITORY "${TAPESISTER_CDP8_GIT_REPOSITORY}"
      GIT_TAG "${TAPESISTER_CDP8_GIT_TAG}"
      GIT_SHALLOW FALSE
      GIT_PROGRESS TRUE
      UPDATE_DISCONNECTED TRUE
      BINARY_DIR "${cdp8_binary_dir}"
      CMAKE_ARGS ${cdp8_cmake_args}
      BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR>
                    --config $<CONFIG>
                    --target ${TAPESISTER_CDP8_REQUIRED_PROGRAMS}
      INSTALL_COMMAND ""
      USES_TERMINAL_DOWNLOAD TRUE
      USES_TERMINAL_CONFIGURE TRUE
      USES_TERMINAL_BUILD TRUE)
  endif()

  add_dependencies("${application_target}" tapesister_cdp8_runtime)
  add_custom_command(TARGET "${application_target}" POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
      "-DCDP8_RUNTIME_DIR=${cdp8_source_dir}/NewRelease"
      "-DCDP8_SOURCE_DIR=${cdp8_source_dir}"
      "-DCDP8_SOURCE_ARCHIVE=${TAPESISTER_CDP8_SOURCE_ARCHIVE}"
      "-DCDP8_GIT_TAG=${TAPESISTER_CDP8_GIT_TAG}"
      "-DCDP8_DESTINATION=$<TARGET_FILE_DIR:${application_target}>"
      "-DTAPESISTER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      "-DPROGRAM_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
      -P "${TAPESISTER_CDP8_CMAKE_DIR}/StageCDP8Runtime.cmake"
    COMMENT "Staging the pinned CDP8 runtime and corresponding source"
    VERBATIM)
endfunction()
