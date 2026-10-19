# Minimal, replaceable LGPL FFmpeg libraries; no network, encoders, GPL codecs,
# external codec dependencies or player executable. Source accompanies bundles.
option(TAPESISTER_BUNDLE_VIDEO "Build and stage the pinned video decoder" ON)
if(TAPESISTER_BUNDLE_VIDEO)
  include(ExternalProject)
  find_program(TS_VIDEO_MAKE NAMES gmake make REQUIRED)
  find_program(TS_VIDEO_SH NAMES sh bash REQUIRED)
  set(ts_video_prefix "${CMAKE_BINARY_DIR}/video")
  set(ts_video_source "${CMAKE_BINARY_DIR}/_deps/ffmpeg-src")
  file(MAKE_DIRECTORY "${ts_video_prefix}/include")
  set(ts_video_byproducts "")
  set(ts_video_libraries "")
  foreach(pair avformat:61 avcodec:61 swscale:8 avutil:59)
    string(REPLACE ":" ";" parts "${pair}")
    list(GET parts 0 name)
    list(GET parts 1 major)
    if(WIN32)
      set(location "${ts_video_prefix}/bin/${name}-${major}.dll")
      set(implib "${ts_video_prefix}/lib/lib${name}.dll.a")
      list(APPEND ts_video_byproducts "${implib}")
    elseif(APPLE)
      set(location "${ts_video_prefix}/lib/lib${name}.dylib")
    else()
      set(location "${ts_video_prefix}/lib/lib${name}.so")
    endif()
    add_library(ts_video_${name} SHARED IMPORTED GLOBAL)
    set_target_properties(ts_video_${name} PROPERTIES IMPORTED_LOCATION "${location}"
      INTERFACE_INCLUDE_DIRECTORIES "${ts_video_prefix}/include")
    if(WIN32)
      set_target_properties(ts_video_${name} PROPERTIES IMPORTED_IMPLIB "${implib}")
    endif()
    list(APPEND ts_video_libraries ts_video_${name})
    list(APPEND ts_video_byproducts "${location}")
  endforeach()
  set(ts_video_platform "")
  if(WIN32)
    list(APPEND ts_video_platform --target-os=mingw32)
  elseif(APPLE)
    list(APPEND ts_video_platform --install-name-dir=@rpath)
  endif()
  ExternalProject_Add(ts_ffmpeg
    URL https://ffmpeg.org/releases/ffmpeg-7.1.4.tar.xz
    URL_HASH SHA256=71f4aac3573ed9060489cb62526a6c7dda815ae10993789611acd7be9fa9fbf4
    DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/ffmpeg-download"
    SOURCE_DIR "${ts_video_source}"
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND ${TS_VIDEO_SH} <SOURCE_DIR>/configure --prefix=${ts_video_prefix}
      --cc=${CMAKE_C_COMPILER} --disable-autodetect --disable-everything
      --disable-programs --disable-doc --disable-network --disable-debug
      --disable-x86asm --disable-static --enable-shared
      --disable-avdevice --disable-avfilter --disable-swresample --disable-postproc
      --enable-avcodec --enable-avformat --enable-avutil --enable-swscale
      --enable-protocol=file --enable-demuxer=mov,matroska,avi,mpegps,mpegts,gif
      --enable-decoder=h264,hevc,vp8,vp9,mpeg4,mpeg1video,mpeg2video,mjpeg,rawvideo,ffv1,prores,gif
      --enable-parser=h264,hevc,vp8,vp9,mpeg4video,mpegvideo,mjpeg
      ${ts_video_platform}
    BUILD_COMMAND ${TS_VIDEO_MAKE} -j2
    INSTALL_COMMAND ${TS_VIDEO_MAKE} install
      COMMAND ${CMAKE_COMMAND} -DVIDEO_PREFIX=${ts_video_prefix}
        -DVIDEO_SOURCE=${ts_video_source} -DBUNDLE_DIR=${CMAKE_BINARY_DIR}
        -DRECIPE=${CMAKE_CURRENT_LIST_FILE} -P ${CMAKE_CURRENT_LIST_DIR}/StageVideo.cmake
    BUILD_BYPRODUCTS ${ts_video_byproducts}
  )
  foreach(lib IN LISTS ts_video_libraries)
    add_dependencies(${lib} ts_ffmpeg)
  endforeach()
  target_link_libraries(tapesister_core PUBLIC ${ts_video_libraries})
  add_dependencies(tapesister_core ts_ffmpeg)
  if(APPLE)
    set(CMAKE_BUILD_RPATH "@loader_path/video/lib")
  else()
    set(CMAKE_BUILD_RPATH "$ORIGIN/video/lib")
  endif()
  set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(TS_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
  target_link_libraries(tapesister_core PUBLIC PkgConfig::TS_FFMPEG)
endif()
target_sources(tapesister_core PRIVATE src/ts_mandala_video.c)
