file(MAKE_DIRECTORY "${BUNDLE_DIR}/licenses/ffmpeg")
file(COPY "${VIDEO_SOURCE}/COPYING.LGPLv2.1" "${VIDEO_SOURCE}/LICENSE.md"
  "${VIDEO_SOURCE}/ffbuild/config.log" "${RECIPE}"
  DESTINATION "${BUNDLE_DIR}/licenses/ffmpeg")
file(COPY "${BUNDLE_DIR}/ffmpeg-download/ffmpeg-7.1.4.tar.xz"
  DESTINATION "${BUNDLE_DIR}/licenses/ffmpeg")
# Windows loader also needs these next to the executable for tests/Explorer.
file(GLOB dlls "${VIDEO_PREFIX}/bin/*.dll")
if(dlls)
  file(COPY ${dlls} DESTINATION "${BUNDLE_DIR}")
endif()
