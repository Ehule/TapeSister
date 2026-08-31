include("${TAPESISTER_SOURCE_DIR}/cmake/CDP8Manifest.cmake")

if(NOT IS_DIRECTORY "${CDP8_RUNTIME_DIR}")
  message(FATAL_ERROR "CDP8 runtime output was not produced: ${CDP8_RUNTIME_DIR}")
endif()

set(cdp8_bin_destination "${CDP8_DESTINATION}/cdp/bin")
set(license_destination "${CDP8_DESTINATION}/licenses")
file(MAKE_DIRECTORY "${cdp8_bin_destination}" "${license_destination}")

foreach(program IN LISTS TAPESISTER_CDP8_REQUIRED_PROGRAMS)
  set(program_path "${CDP8_RUNTIME_DIR}/${program}${PROGRAM_SUFFIX}")
  if(NOT EXISTS "${program_path}")
    message(FATAL_ERROR "Required CDP8 program was not built: ${program_path}")
  endif()
  file(COPY "${program_path}" DESTINATION "${cdp8_bin_destination}")
endforeach()

file(COPY "${CDP8_SOURCE_DIR}/LICENSE"
     DESTINATION "${license_destination}")
file(RENAME "${license_destination}/LICENSE"
            "${license_destination}/CDP8-LGPL-2.1.txt")
file(COPY "${TAPESISTER_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
     DESTINATION "${license_destination}")
file(COPY "${TAPESISTER_SOURCE_DIR}/docs/CDP8_RUNTIME.md"
     DESTINATION "${license_destination}")

set(source_archive
    "${license_destination}/CDP8-source-${CDP8_GIT_TAG}.zip")
if(CDP8_SOURCE_ARCHIVE)
  if(NOT EXISTS "${CDP8_SOURCE_ARCHIVE}")
    message(FATAL_ERROR
      "TAPESISTER_CDP8_SOURCE_ARCHIVE does not exist: ${CDP8_SOURCE_ARCHIVE}")
  endif()
  configure_file("${CDP8_SOURCE_ARCHIVE}" "${source_archive}" COPYONLY)
else()
  find_program(GIT_EXECUTABLE git)
  if(NOT GIT_EXECUTABLE)
    message(FATAL_ERROR
      "Git is required to create the exact CDP8 source archive")
  endif()
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CDP8_SOURCE_DIR}" archive
            --format=zip "--output=${source_archive}" "${CDP8_GIT_TAG}"
    RESULT_VARIABLE archive_result
    ERROR_VARIABLE archive_error)
  if(NOT archive_result EQUAL 0)
    message(FATAL_ERROR
      "Could not archive the exact CDP8 source. Supply "
      "TAPESISTER_CDP8_SOURCE_ARCHIVE for a non-Git source tree.\n"
      "${archive_error}")
  endif()
endif()
