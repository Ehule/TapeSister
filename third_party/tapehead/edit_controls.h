/* Extracted from ft2_keyboard.c, Tapehead f053d96 (BSD-3-Clause).
 * Only global operands and the UI redraw hook are adapted.
 * Regenerate with scripts/import-tapehead-drawing.py. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

static inline uint8_t tapeheadChangeEditSkip(uint8_t editRowSkip, bool shiftPressed)
{
	if (shiftPressed)
	{
		// decrease edit skip
		if (editRowSkip == 0)
			editRowSkip = 16;
		else
			editRowSkip--;
	}
	else
	{
		// increase edit skip
		if (editRowSkip == 16)
			editRowSkip = 0;
		else
			editRowSkip++;
	}
	return editRowSkip;
}
