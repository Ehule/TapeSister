#pragma once
#include <stdbool.h>
#include <stdint.h>

static inline bool tapeheadTrackUsesIndependentTransportVisual(
	bool transportRunning, bool fastTrackEnabled,
	bool individualLengthEnabled, bool frozen)
{
	return transportRunning &&
		(fastTrackEnabled || individualLengthEnabled || frozen);
}

static inline int32_t tapeheadTransportVisualPageStart(int32_t playbackRow,
	int32_t rowsOnScreen)
{
	if (playbackRow <= 0 || rowsOnScreen <= 0)
		return 0;
	return (playbackRow / rowsOnScreen) * rowsOnScreen;
}
