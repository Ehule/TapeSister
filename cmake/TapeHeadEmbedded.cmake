# The application sources are vendored at one immutable TapeHead revision.
# TapeSister owns main(), SDL, MIDI, the audio device, and project/tile storage.
set(TH_EMBED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tapehead/application/src")
file(GLOB TH_EMBED_SOURCES CONFIGURE_DEPENDS
  "${TH_EMBED_ROOT}/*.c" "${TH_EMBED_ROOT}/gfxdata/*.c"
  "${TH_EMBED_ROOT}/mixer/*.c" "${TH_EMBED_ROOT}/scopes/*.c"
  "${TH_EMBED_ROOT}/modloaders/*.c" "${TH_EMBED_ROOT}/smploaders/*.c")
list(REMOVE_ITEM TH_EMBED_SOURCES
  "${TH_EMBED_ROOT}/ft2_main.c" "${TH_EMBED_ROOT}/tape_link.c"
  "${TH_EMBED_ROOT}/tape_companion.c" "${TH_EMBED_ROOT}/ft2_fasttracks_core.c")
add_library(tapesister_tapehead STATIC ${TH_EMBED_SOURCES} src/ts_tapehead_embed.c)
target_include_directories(tapesister_tapehead PRIVATE ${TH_EMBED_ROOT} include)
target_compile_definitions(tapesister_tapehead PRIVATE TAPEHEAD_EMBEDDED SDL_MAIN_HANDLED _DEFAULT_SOURCE)
target_compile_definitions(tapesister_tapehead PRIVATE
  textOutTiny=th_textOutTiny patternXToCursorObject=th_patternXToCursorObject
  writePattern=th_writePattern pattTwoHexOut=th_pattTwoHexOut)
target_link_libraries(tapesister_tapehead PUBLIC SDL2::SDL2 tapesister_core)
if(UNIX AND NOT APPLE)
  target_link_libraries(tapesister_tapehead PRIVATE rt dl)
endif()
if(WIN32)
  target_link_libraries(tapesister_tapehead PRIVATE shlwapi shell32)
endif()

if(APPLE)
  find_library(TH_ICONV iconv REQUIRED)
  target_link_libraries(tapesister_tapehead PRIVATE ${TH_ICONV})
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(src/ts_tapehead_embed.c PROPERTIES COMPILE_OPTIONS
    "-Wall;-Wextra;-Werror=implicit-function-declaration;-Werror=int-conversion;-Werror=incompatible-pointer-types")
endif()
