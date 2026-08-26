#ifndef TAPESISTER_VERSION_H
#define TAPESISTER_VERSION_H

/*
 * Short, user-visible identity for comparison builds.  Bump this value for
 * every build handed out for testing.  Keep it to six visible characters so
 * it remains inside the reserved badge.  Build systems may override it with
 * -DTAPESISTER_BUILD_MARKER=\"...\" for one-off variants.
 */
#ifndef TAPESISTER_BUILD_MARKER
#define TAPESISTER_BUILD_MARKER "PR11.1"
#endif

#define TAPESISTER_BUILD_MARKER_MAX_CHARS 6

#define TAPESISTER_WINDOW_TITLE \
    "TapeSister [" TAPESISTER_BUILD_MARKER "]"
#define TAPESISTER_REC_BANK_WINDOW_TITLE \
    "TapeSister - REC BANK [" TAPESISTER_BUILD_MARKER "]"
#define TAPESISTER_SISTER_WINDOW_TITLE \
    "TapeSister - Sister Machine [" TAPESISTER_BUILD_MARKER "]"

#endif
