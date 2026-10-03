/* Extracted from Tapehead f053d96. BSD-3-Clause; see CODE-LICENSE.txt.
 * Regenerate with scripts/import-tapehead-drawing.py.
 * Host adaptations are recorded in pattern_draw.patch. */

#define CURSOR_BREATHE_FRAMES 120

static uint8_t cursorBreatheFrame;

static note_t emptyPattern[MAX_CHANNELS * MAX_PATT_LEN];

static const uint8_t *font4Ptr;

typedef struct pattLayout_t
{
	uint8_t x[11], width[11], fontType, charW, noteSize;
} pattLayout_t;

static const uint8_t vol2charTab1[16] = { 39, 0, 1, 2, 3, 4, 36, 52, 53, 54, 28, 31, 25, 58, 59, 22 };

static const uint8_t vol2charTab2[16] = { 42, 0, 1, 2, 3, 4, 36, 37, 38, 39, 28, 31, 25, 40, 41, 22 };

/* Tapehead's eight-channel layout, volume visible. Other width modes removed. */
static const pattLayout_t patternLayout =
    {{3,27,31,35,39,43,47,51,55,59,63}, {24,4,4,4,4,4,4,4,4,4,4}, FONT_TYPE3,4,1};

static const uint8_t sharpNote1Char_med[12] = { 12*8, 12*8, 13*8, 13*8, 14*8, 15*8, 15*8, 16*8, 16*8, 10*8, 10*8, 11*8 };

static const uint16_t sharpNote2Char_med[12] = { 36*8, 37*8, 36*8, 37*8, 36*8, 36*8, 37*8, 36*8, 37*8, 36*8, 37*8, 36*8 };

static const uint8_t flatNote1Char_med[12] = { 12*8, 13*8, 13*8, 14*8, 14*8, 15*8, 16*8, 16*8, 10*8, 10*8, 11*8, 11*8 };

static const uint16_t flatNote2Char_med[12] = { 36*8, 38*8, 36*8, 38*8, 36*8, 36*8, 38*8, 36*8, 38*8, 36*8, 38*8, 36*8 };

static const uint8_t noteTab1[96] =
{
	0,1,2,3,4,5,6,7,8,9,10,11,
	0,1,2,3,4,5,6,7,8,9,10,11,
	0,1,2,3,4,5,6,7,8,9,10,11,
	0,1,2,3,4,5,6,7,8,9,10,11,
	0,1,2,3,4,5,6,7,8,9,10,11,
	0,1,2,3,4,5,6,7,8,9,10,11,
	0,1,2,3,4,5,6,7,8,9,10,11,
	0,1,2,3,4,5,6,7,8,9,10,11
};

static const uint8_t noteTab2[96] =
{
	0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,
	2,2,2,2,2,2,2,2,2,2,2,2,
	3,3,3,3,3,3,3,3,3,3,3,3,
	4,4,4,4,4,4,4,4,4,4,4,4,
	5,5,5,5,5,5,5,5,5,5,5,5,
	6,6,6,6,6,6,6,6,6,6,6,6,
	7,7,7,7,7,7,7,7,7,7,7,7
};

static const uint8_t hex2Dec[256] =
{
	  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
	 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
	 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
	 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
	 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
	 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
	 96, 97, 98, 99,100,101,102,103,104,105,
	112,113,114,115,116,117,118,119,120,121,
	128,129,130,131,132,133,134,135,136,137,
	144,145,146,147,148,149,150,151,152,153,

	  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
	 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
	 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
	 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
	 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
	 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
	 96, 97, 98, 99,100,101,102,103,104,105,
	112,113,114,115,116,117,118,119,120,121,
	128,129,130,131,132,133,134,135,136,137,
	144,145,146,147,148,149,150,151,152,153,

	 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
	16,17,18,19,20,21,22,23,24,25,
	32,33,34,35,36,37,38,39,40,41,
	48,49,50,51,52,53,54,55,56,57,
	64,65,66,67,68,69,70,71,72,73,
	80,81,82,83,84,85
};

static const uint16_t chanWidths[6] = { 141, 141, 93, 69, 45, 45 };

static uint8_t getCursorBreatheLevel(void);

static uint32_t breatheColorToward(uint32_t color, uint32_t background);

static const pattLayout_t *getPatternLayout(void);

uint8_t patternXToCursorObject(int32_t x);

static void writeCursor(void);

static void pattLayoutCharOut(uint32_t x, uint32_t y, uint8_t chr,
	const pattLayout_t *layout, uint32_t color);

static void pattLayoutPlaceholderOut(uint32_t x, uint32_t y,
	const pattLayout_t *layout, uint32_t color);

static void drawAdaptiveCell(uint32_t x, uint32_t y, const note_t *n,
	uint32_t noteColor, uint32_t instrColor, uint32_t volColor,
	uint32_t tuneColor, uint32_t effectColor);

static void drawChannelNumbering(uint16_t yPos);

static void drawRowNums(int32_t yPos, uint8_t row, bool selectedRowFlag);

static void drawFastTracksPOCLed(uint16_t x, uint16_t y, uint32_t color);

static void drawFastTracksPOCStatus(uint16_t yPos, const fastTracksSnapshot_t *snapshot);

static void drawDirectHLine(uint16_t x, uint16_t y, uint16_t width,
	uint32_t color);

static void drawDirectVLine(uint16_t x, uint16_t y, uint16_t height,
	uint32_t color);

static void drawControlEjectSymbol(uint16_t x, uint16_t y, uint32_t color);

static void drawTrackLengthStatus(uint16_t yPos, uint16_t patternNumber);

static uint32_t dimPatternColor(uint32_t color);

static void drawTrackPlayheadOutline(uint16_t x, uint16_t y, uint16_t width,
	uint16_t height, uint32_t color);

void writePattern(int32_t currRow, int32_t currPattern);

void pattTwoHexOut(uint32_t xPos, uint32_t yPos, uint8_t val, uint32_t color);

static void pattCharOut(uint32_t xPos, uint32_t yPos, uint8_t chr, uint8_t fontType, uint32_t color);

static void drawEmptyNoteMedium(uint32_t xPos, uint32_t yPos, uint32_t color);

static void drawKeyOffMedium(uint32_t xPos, uint32_t yPos, uint32_t color);

static void drawNoteMedium(uint32_t xPos, uint32_t yPos, int32_t noteNum, uint32_t color);

static uint8_t getCursorBreatheLevel(void)
{
	/*
	** Two-second triangular breathing curve at 60Hz:
	** 255 -> 140 -> 255. It never becomes faint enough to disappear.
	*/
	const uint16_t halfCycle = CURSOR_BREATHE_FRAMES / 2;
	uint16_t distance = cursorBreatheFrame;
	if (distance > halfCycle)
		distance = CURSOR_BREATHE_FRAMES - distance;

	return (uint8_t)(255 - ((distance * 115) / halfCycle));
}

static uint32_t breatheColorToward(uint32_t color, uint32_t background)
{
	const uint16_t level = getCursorBreatheLevel();
	const uint16_t inverse = 255 - level;

	const uint8_t r = (uint8_t)(((RGB32_R(color) * level) + (RGB32_R(background) * inverse)) / 255);
	const uint8_t g = (uint8_t)(((RGB32_G(color) * level) + (RGB32_G(background) * inverse)) / 255);
	const uint8_t b = (uint8_t)(((RGB32_B(color) * level) + (RGB32_B(background) * inverse)) / 255);
	return RGB32(r, g, b);
}

static const pattLayout_t *getPatternLayout(void)
{
	return &patternLayout;
}

uint8_t patternXToCursorObject(int32_t x)
{
	const pattLayout_t *layout = getPatternLayout();
	uint8_t bestObject = CURSOR_NOTE;
	int32_t bestDistance = 0x7FFFFFFF;

	for (uint8_t object = CURSOR_NOTE; object <= CURSOR_EFX2; object++)
	{
		if (!config.ptnShowVolColumn && (object == CURSOR_VOL1 || object == CURSOR_VOL2))
			continue;

		const int32_t center = layout->x[object] + (layout->width[object] / 2);
		const int32_t distance = ABS(x - center);
		if (distance < bestDistance)
		{
			bestDistance = distance;
			bestObject = object;
		}
	}

	return bestObject;
}

static void writeCursor(void)
{
	if (editor.ptnCursorY < host.layout.grid_y ||
		editor.ptnCursorY + TS_TRACKER_ROW_HEIGHT > host.layout.bottom)
		return;
	const pattLayout_t *layout = getPatternLayout();
	int32_t xPos = TS_TRACKER_LANE_X + layout->x[cursor.object];
	const int32_t width = layout->width[cursor.object];

	ASSERT(editor.ptnCursorY > 0 && xPos > 0 && width > 0);
	xPos += ((cursor.ch - ui.channelOffset) * ui.patternChannelWidth);

	/*
	** Keep the pattern data untouched and draw a one-pixel locator around the
	** active field. The color also communicates whether keyboard input will
	** edit pattern data, without requiring the performer to look at the status
	** readout above the editor. Recording modes use the edit color as well.
	*/
	const bool editCursor = playMode == PLAYMODE_EDIT ||
	                        playMode == PLAYMODE_RECPATT ||
	                        playMode == PLAYMODE_RECSONG;
	uint32_t cursorColor = video.palette[editCursor ? PAL_CURSOR_EDIT : PAL_CURSOR_NAV];
	cursorColor = breatheColorToward(cursorColor, video.palette[PAL_DESKTOP]);

	uint32_t *topPtr = &video.frameBuffer[(editor.ptnCursorY * SCREEN_W) + xPos];
	uint32_t *bottomPtr = topPtr + ((TS_TRACKER_ROW_HEIGHT - 1) * SCREEN_W);
	for (int32_t x = 0; x < width; x++)
	{
		topPtr[x] = cursorColor;
		bottomPtr[x] = cursorColor;
	}

	for (int32_t y = 1; y < TS_TRACKER_ROW_HEIGHT - 1; y++)
	{
		uint32_t *rowPtr = topPtr + (y * SCREEN_W);
		rowPtr[0] = cursorColor;
		if (width > 1)
			rowPtr[width-1] = cursorColor;
	}
}

static void pattLayoutCharOut(uint32_t x, uint32_t y, uint8_t chr,
	const pattLayout_t *layout, uint32_t color)
{
	if (chr == HOST_PLACEHOLDER)
	{
		pattLayoutPlaceholderOut(x, y, layout, color);
		return;
	}
	if (chr == HOST_MISSING_ALIAS)
	{
		hostQuestionMark(x, y, color);
		return;
	}
	if (layout->charW != 3)
	{
		pattCharOut(x, y, chr, layout->fontType, color);
		return;
	}

	/* The twelve-channel mode has 48 pixels per cell. Clip the small glyph to
	** three columns so adjacent instrument/tuning/effect characters never overlap. */
	const uint8_t *src = &bmp.font3[chr * FONT3_CHAR_W];
	uint32_t *dst = &video.frameBuffer[(y * SCREEN_W) + x];
	for (int32_t row = 0; row < FONT3_CHAR_H; row++)
	{
		/* Preserve both outside strokes and merge the two center columns. This
		** keeps M/N and hexadecimal glyphs recognizable instead of truncating
		** their right edge. */
		if (src[0] != 0) dst[0] = color;
		if (src[1] != 0 || src[2] != 0) dst[1] = color;
		if (src[3] != 0) dst[2] = color;

		src += FONT3_WIDTH;
		dst += SCREEN_W;
	}
}

static void pattLayoutPlaceholderOut(uint32_t x, uint32_t y,
	const pattLayout_t *layout, uint32_t color)
{
	/* A placeholder represents one editable character, not the space beside it.
	** Drawing the font's period glyph filled too much of the tightly packed
	** layouts and made neighboring empty fields look like one continuous run. */
	video.frameBuffer[((y + 3) * SCREEN_W) + x + (layout->charW / 2)] = color;
}

static void drawAdaptiveCell(uint32_t x, uint32_t y, const note_t *n,
	uint32_t noteColor, uint32_t instrColor, uint32_t volColor,
	uint32_t tuneColor, uint32_t effectColor)
{
	const pattLayout_t *layout = getPatternLayout();
	const uint8_t *px = layout->x;

	if (!hostDrawExtendedNote(x + px[0], y, n, noteColor))
	{
		if (n->note == NOTE_OFF) drawKeyOffMedium(x + px[0], y, noteColor);
		else if (n->note > 0 && n->note <= 96) drawNoteMedium(x + px[0], y, n->note, noteColor);
		else drawEmptyNoteMedium(x + px[0], y, noteColor);
	}

	/* Empty instrument numbers intentionally retain FT2's 00 convention. */
	pattLayoutCharOut(x + px[1], y, hostCellChar(n, 1, n->instr >> 4), layout, instrColor);
	pattLayoutCharOut(x + px[2], y, hostCellChar(n, 2, n->instr & 15), layout, instrColor);

	if (config.ptnShowVolColumn)
	{
		if (n->vol < 0x10)
		{
			pattLayoutPlaceholderOut(x + px[3], y, layout, volColor);
			pattLayoutPlaceholderOut(x + px[4], y, layout, volColor);
		}
		else
		{
			const uint8_t vol1 = layout->fontType == FONT_TYPE3 ?
				vol2charTab2[n->vol >> 4] : vol2charTab1[n->vol >> 4];
			pattLayoutCharOut(x + px[3], y, hostCellChar(n, 3, vol1), layout, volColor);
			pattLayoutCharOut(x + px[4], y, hostCellChar(n, 4, n->vol & 15), layout, volColor);
		}
	}

	if (n->tuneType == 0 && n->pendingObject != CURSOR_TUNE2)
	{
		for (int32_t i = 5; i <= 7; i++)
			pattLayoutPlaceholderOut(x + px[i], y, layout, tuneColor);
	}
	else
	{
		pattLayoutCharOut(x + px[5], y, hostCellChar(n, 5, n->tuneType), layout, tuneColor);
		pattLayoutCharOut(x + px[6], y, hostCellChar(n, 6, n->tuneData >> 4), layout, tuneColor);
		pattLayoutCharOut(x + px[7], y, hostCellChar(n, 7, n->tuneData & 15), layout, tuneColor);
	}

	if (!n->hasEffect && n->pendingObject != CURSOR_EFX2)
	{
		for (int32_t i = 8; i <= 10; i++)
			pattLayoutPlaceholderOut(x + px[i], y, layout, effectColor);
	}
	else
	{
		pattLayoutCharOut(x + px[8], y, hostCellChar(n, 8, n->efx), layout, effectColor);
		pattLayoutCharOut(x + px[9], y, hostCellChar(n, 9, n->efxData >> 4), layout, effectColor);
		pattLayoutCharOut(x + px[10], y, hostCellChar(n, 10, n->efxData & 15), layout, effectColor);
	}
}

static void drawChannelNumbering(uint16_t yPos)
{
	uint16_t xPos = TS_TRACKER_LANE_X;
	uint8_t ch = ui.channelOffset + 1;

	for (uint8_t i = 0; i < ui.numChannelsShown; i++)
	{
		char channelText[4];
		snprintf(channelText, sizeof (channelText), "%u", ch);
		textOutTiny(xPos + 1, yPos + 1, channelText,
			video.palette[PAL_MOUSEPT]);

		ch++;
		xPos += ui.patternChannelWidth;
	}
}

static void drawRowNums(int32_t yPos, uint8_t row, bool selectedRowFlag)
{
#define LEFT_ROW_XPOS 9
#define RIGHT_ROW_XPOS (TS_TRACKER_LANE_RIGHT + 7)

	uint32_t pixVal;

	// set color based on some conditions

	if (selectedRowFlag)
		pixVal = video.palette[PAL_FORGRND];
	else if (config.ptnLineLight && !(row & 3))
		pixVal = video.palette[PAL_BLCKTXT];
	else
		pixVal = video.palette[PAL_PATTEXT];

	if (!config.ptnHex)
		row = hex2Dec[row];

	const uint8_t *src1Ptr = &font4Ptr[(row   >> 4) * FONT4_CHAR_W];
	const uint8_t *src2Ptr = &font4Ptr[(row & 0x0F) * FONT4_CHAR_W];
	uint32_t *dst1Ptr = &video.frameBuffer[(yPos * SCREEN_W) + LEFT_ROW_XPOS];
	uint32_t *dst2Ptr = dst1Ptr + (RIGHT_ROW_XPOS - LEFT_ROW_XPOS);

	for (int32_t y = 0; y < FONT4_CHAR_H; y++)
	{
		for (int32_t x = 0; x < FONT4_CHAR_W; x++)
		{
			if (src1Ptr[x])
			{
				dst1Ptr[x] = pixVal; // left side
				dst2Ptr[x] = pixVal; // right side
			}

			if (src2Ptr[x])
			{
				dst1Ptr[FONT4_CHAR_W+x] = pixVal; // left side
				dst2Ptr[FONT4_CHAR_W+x] = pixVal; // right side
			}
		}

		src1Ptr += FONT4_WIDTH;
		src2Ptr += FONT4_WIDTH;
		dst1Ptr += SCREEN_W;
		dst2Ptr += SCREEN_W;
	}
}

static void drawFastTracksPOCLed(uint16_t x, uint16_t y, uint32_t color)
{
	/* Compact 3x3 rounded LED: visible at every supported channel width. */
	video.frameBuffer[( y      * SCREEN_W) + x + 1] = color;
	video.frameBuffer[((y + 1) * SCREEN_W) + x    ] = color;
	video.frameBuffer[((y + 1) * SCREEN_W) + x + 1] = color;
	video.frameBuffer[((y + 1) * SCREEN_W) + x + 2] = color;
	video.frameBuffer[((y + 2) * SCREEN_W) + x + 1] = color;
}

static void drawFastTracksPOCStatus(uint16_t yPos, const fastTracksSnapshot_t *snapshot)
{
	if (!snapshot->masterEnabled)
		return;

	/* A distinct second strip is reserved across the complete tracker width
	** while FastTracks is live. This keeps its diagnostics stable and prevents
	** LEN/CONTROL from painting over ratios or Song Mode state. */
	for (int32_t visibleChannel = 0; visibleChannel < ui.numChannelsShown;
		visibleChannel++)
	{
		const int32_t channelIndex = ui.channelOffset + visibleChannel;
		if (channelIndex < 0 || channelIndex >= song.numChannels)
			continue;

		const uint16_t xPos = (uint16_t)(TS_TRACKER_LANE_X +
			(visibleChannel * ui.patternChannelWidth));
		const uint16_t panelWidth = (uint16_t)(ui.patternChannelWidth - 2);
		fillRect(xPos, yPos, panelWidth, 8, PAL_DESKTOP);
		if (!snapshot->tracks[channelIndex].enabled)
		{
			textOutTiny(xPos + 2, yPos + 1, "FT",
				breatheColorToward(video.palette[PAL_BLCKTXT],
					video.palette[PAL_DESKTOP]));
		}
	}

	for (int32_t fastTrackChannel = 0; fastTrackChannel < MAX_CHANNELS; fastTrackChannel++)
	{
		const fastTracksTrackSnapshot_t *track = &snapshot->tracks[fastTrackChannel];
		if (!track->enabled)
			continue;

		if (fastTrackChannel < ui.channelOffset || fastTrackChannel >= ui.channelOffset + ui.numChannelsShown)
			continue;

		const int32_t visibleChannel = fastTrackChannel - ui.channelOffset;
		const int32_t xPos = TS_TRACKER_LANE_X + (visibleChannel * ui.patternChannelWidth);
		const uint8_t numerator = track->ratioNumerator;
		const uint8_t denominator = track->ratioDenominator;
		const int32_t sourceRow = track->sourceRow;
		const fastTracksMode_t transportMode = track->mode;
		const bool songMode = transportMode == FAST_TRACKS_MODE_SONG;
		const int32_t sourceOrder = track->sourceOrder;
		const int32_t sourcePattern = track->sourcePattern;
		const bool clutchHeld = track->clutched;
		const bool reversed = track->reversed;
		const uint16_t panelWidth = (uint16_t)(ui.patternChannelWidth - 2);
		const uint8_t panelColor = clutchHeld ? PAL_BLCKMRK : PAL_DESKTOP;
		fillRect((uint16_t)xPos, yPos, panelWidth, 8, panelColor);

		char ratioText[8];
		snprintf(ratioText, sizeof (ratioText), "%u:%u", numerator, denominator);
		const int32_t ratioWidth = (int32_t)strlen(ratioText) * FONT3_CHAR_W;
		const int32_t ratioX = xPos + 2;
		const uint32_t ratioColor = video.palette[PAL_BLCKTXT];
		textOutTiny(ratioX, yPos + 1, ratioText, ratioColor);

		int32_t statusX = ratioX + ratioWidth + 2;
		if (reversed)
		{
			fillRect((uint16_t)statusX, yPos, 5, 8, PAL_BLCKMRK);
			textOutTiny(statusX + 1, yPos + 1, hostDirectionBadge(fastTrackChannel),
				video.palette[PAL_FASTTRACKS_PHASE]);
			statusX += 6;
		}

		/* Keep a small right margin so the LED bank cannot be clipped by the
		** next channel separator. The three-pixel shift also leaves the Song
		** badge its own centered visual zone. */
		const int32_t ledBankX = xPos + panelWidth - 15;
		const uint16_t lagLedX = (uint16_t)ledBankX;
		const uint16_t syncLedX = (uint16_t)(ledBankX + 4);
		const uint16_t leadLedX = (uint16_t)(ledBankX + 8);
		const uint32_t ledOffColor = breatheColorToward(video.palette[PAL_BLCKTXT], video.palette[panelColor]);
		drawFastTracksPOCLed(lagLedX,  (uint16_t)(yPos + 2), ledOffColor);
		drawFastTracksPOCLed(syncLedX, (uint16_t)(yPos + 2), ledOffColor);
		drawFastTracksPOCLed(leadLedX, (uint16_t)(yPos + 2), ledOffColor);

		if (songMode)
		{
			/* Wide lanes report order>pattern. Ten/twelve-channel views retain the
			** more important pattern number in a compact centered badge so the
			** phase LEDs remain unobstructed. */
			char songText[8];
			if (panelWidth >= 64)
			{
				snprintf(songText, sizeof (songText), "%02X>%02X",
					sourceOrder & 0xFF, sourcePattern & 0xFF);
			}
			else
			{
				snprintf(songText, sizeof (songText), "%02X",
					sourcePattern & 0xFF);
			}

			const int32_t badgeWidth =
				((int32_t)strlen(songText) * FONT3_CHAR_W) + 2;
			const int32_t centeredBadgeX = xPos +
				(((int32_t)panelWidth - badgeWidth) / 2);
			const int32_t maxBadgeX = ledBankX - badgeWidth - 1;
			int32_t badgeX = MIN(centeredBadgeX, maxBadgeX);
			if (statusX <= maxBadgeX)
				badgeX = MAX(badgeX, statusX);
			fillRect((uint16_t)badgeX, yPos, (uint16_t)badgeWidth, 8,
				PAL_BLCKMRK);
			textOutTiny(badgeX + 1, yPos + 1, songText,
				video.palette[PAL_FASTTRACKS_SONG]);
		}

		if (!songPlaying)
			continue;
		const bool masterAligned = track->masterAligned;
		if (masterAligned)
		{
			/* Exact synchronization keeps its own configurable status color. */
			const uint32_t syncColor = breatheColorToward(
				video.palette[PAL_FASTTRACKS_SYNC], video.palette[panelColor]);
			drawFastTracksPOCLed(syncLedX, (uint16_t)(yPos + 2), syncColor);
		}
		else if (!songMode)
		{
			const int32_t numRows = fastTracksPOCGetFastTrackLength(
				song.pattNum, fastTrackChannel);
			int32_t phaseOffset = sourceRow - song.row;
			phaseOffset %= numRows;
			if (phaseOffset < 0)
				phaseOffset += numRows;
			if (phaseOffset > numRows / 2)
				phaseOffset -= numRows;

			/* FastTracks phase has its own palette entry. During clutch, the
			** block-mark color remains the temporary emphasis. */
			const uint32_t phaseBaseColor = clutchHeld ? video.palette[PAL_BLCKMRK] :
				video.palette[PAL_FASTTRACKS_PHASE];
			const uint32_t phaseColor = breatheColorToward(phaseBaseColor, video.palette[panelColor]);
			if (phaseOffset < 0)
			{
				drawFastTracksPOCLed(lagLedX, (uint16_t)(yPos + 2), phaseColor);
			}
			else if (phaseOffset > 0)
			{
				drawFastTracksPOCLed(leadLedX, (uint16_t)(yPos + 2), phaseColor);
			}
			else
			{
				/* Same displayed row but not truly transport-aligned: illuminate both
				** sides instead of falsely claiming a direction or exact sync. */
				drawFastTracksPOCLed(lagLedX,  (uint16_t)(yPos + 2), phaseColor);
				drawFastTracksPOCLed(leadLedX, (uint16_t)(yPos + 2), phaseColor);
			}
		}
	}
}

static void drawDirectHLine(uint16_t x, uint16_t y, uint16_t width,
	uint32_t color)
{
	ASSERT(x < SCREEN_W && y < SCREEN_H && (x + width) <= SCREEN_W);

	uint32_t *dstPtr = &video.frameBuffer[(y * SCREEN_W) + x];
	for (uint16_t i = 0; i < width; i++)
		dstPtr[i] = color;
}

static void drawDirectVLine(uint16_t x, uint16_t y, uint16_t height,
	uint32_t color)
{
	ASSERT(x < SCREEN_W && y < SCREEN_H && (y + height) <= SCREEN_H);

	uint32_t *dstPtr = &video.frameBuffer[(y * SCREEN_W) + x];
	for (uint16_t i = 0; i < height; i++)
	{
		*dstPtr = color;
		dstPtr += SCREEN_W;
	}
}

static void drawControlEjectSymbol(uint16_t x, uint16_t y, uint32_t color)
{
	drawDirectHLine((uint16_t)(x + 3), y, 1, color);
	drawDirectHLine((uint16_t)(x + 2), (uint16_t)(y + 1), 3, color);
	drawDirectHLine((uint16_t)(x + 1), (uint16_t)(y + 2), 5, color);
	drawDirectHLine(x, (uint16_t)(y + 4), 7, color);
	drawDirectHLine(x, (uint16_t)(y + 5), 7, color);
}

static void drawTrackLengthStatus(uint16_t yPos, uint16_t patternNumber)
{
	const int32_t controlTrack = fastTracksPOCGetControlTrack(patternNumber);
	const bool topologyBypassed = fastTracksPOCLengthTopologyIsBypassed();
	for (int32_t visibleChannel = 0; visibleChannel < ui.numChannelsShown;
		visibleChannel++)
	{
		const int32_t channelIndex = ui.channelOffset + visibleChannel;
		if (channelIndex < 0 || channelIndex >= song.numChannels)
			continue;

		const int32_t xPos = TS_TRACKER_LANE_X + (visibleChannel * ui.patternChannelWidth);
		const uint16_t panelWidth = (uint16_t)(ui.patternChannelWidth - 2);
		const uint16_t storedLength =
			fastTracksPOCGetTrackLength(patternNumber, channelIndex);
		fillRect((uint16_t)xPos, yPos, panelWidth, 8, PAL_DESKTOP);

		char lengthText[16];
		if (storedLength != 0)
			snprintf(lengthText, sizeof (lengthText), "LEN%u", storedLength);
		else
			strcpy(lengthText, "LEN OFF");
		uint32_t lengthColor = storedLength != 0 ? video.palette[PAL_BLCKTXT] :
			breatheColorToward(video.palette[PAL_BLCKTXT],
				video.palette[PAL_DESKTOP]);
		if (topologyBypassed)
			lengthColor = dimPatternColor(lengthColor);
		textOutTiny(xPos + 10, yPos + 1, lengthText, lengthColor);

		const bool isControl = controlTrack == channelIndex;
		uint32_t controlColor = isControl ?
			video.palette[PAL_CONTROL_PLAYHEAD] :
			breatheColorToward(video.palette[PAL_BLCKTXT],
				video.palette[PAL_DESKTOP]);
		if (topologyBypassed)
			controlColor = dimPatternColor(controlColor);
		drawControlEjectSymbol((uint16_t)(xPos + panelWidth - 11),
			(uint16_t)(yPos + 1),
			controlColor);
	}
}

static uint32_t dimPatternColor(uint32_t color)
{
	const uint32_t background = video.palette[PAL_DESKTOP];
	return RGB32(
		(uint8_t)((RGB32_R(color) + (RGB32_R(background) * 2)) / 3),
		(uint8_t)((RGB32_G(color) + (RGB32_G(background) * 2)) / 3),
		(uint8_t)((RGB32_B(color) + (RGB32_B(background) * 2)) / 3));
}

static void drawTrackPlayheadOutline(uint16_t x, uint16_t y, uint16_t width,
	uint16_t height, uint32_t color)
{
	if (width < 2 || height < 2)
		return;

	drawDirectHLine(x, y, width, color);
	drawDirectHLine(x, (uint16_t)(y + height - 1), width, color);
	drawDirectVLine(x, y, height, color);
	drawDirectVLine((uint16_t)(x + width - 1), y, height, color);
}

void writePattern(int32_t currRow, int32_t currPattern)
{
	uint32_t noteTextColors[2];

	/* Draw pattern framework every time (erasing existing content).
	** FT2 doesn't do this. This is quite lazy and consumes more CPU
	** time than needed (overlapped drawing), but it makes the pattern
	** mark/cursor drawing MUCH simpler to implement...
	*/
	drawPatternBorders();

	uint32_t chans = ui.numChannelsShown;
	if (chans > ui.maxVisibleChannels)
		chans = ui.maxVisibleChannels;

	ASSERT(chans >= 2 && chans <= 12);

	const uint32_t chanWidth = chanWidths[(chans / 2) - 1];
	ui.patternChannelWidth = (uint16_t)(chanWidth + 3);

	const uint32_t rowHeight = TS_TRACKER_ROW_HEIGHT;
	const pattCoord_t *pattCoord = &host.coordinates;
	const int32_t rowsOnScreen = pattCoord->numUpperRows + 1 + pattCoord->numLowerRows;
	const int32_t numChannels = ui.numChannelsShown;
	const int32_t numRows = fastTracksPOCGetExtendedPatternLength((uint16_t)currPattern);
	const bool lengthTopologyActive =
		fastTracksPOCLengthTopologyIsActive((uint16_t)currPattern);
	const bool lengthTopologyBypassed =
		fastTracksPOCLengthTopologyIsBypassed();

	/* Native FOLLOW/manual-scroll selects the view; pointer gestures do not
	** alter it. Source-row resolution below is the Tapehead pattern pass. */
	const bool hybridVisuals = host.view.following;
	const int32_t visualMasterRow = host.view.master_row;
	int32_t row = host.view.master_top;
	int32_t textY = pattCoord->upperRowsTextY;

	fastTracksSnapshot_t fastTracksSnapshot;
	fastTracksPOCGetSnapshot(&fastTracksSnapshot);


	noteTextColors[0] = video.palette[PAL_PATTEXT];
	noteTextColors[1] = video.palette[PAL_FORGRND];

	for (int32_t i = 0; i < rowsOnScreen; i++)
	{
		const bool masterRowValid = row >= 0 && row < numRows;
		const bool selectedRowFlag = masterRowValid && row == visualMasterRow;
		if (masterRowValid)
			drawRowNums(textY, (uint8_t)row, selectedRowFlag);

		const int32_t xWidth = ui.patternChannelWidth;
		const uint32_t selectedColor = noteTextColors[selectedRowFlag];
		int32_t xPos = TS_TRACKER_LANE_X;
		for (int32_t j = 0; j < numChannels; j++, xPos += xWidth)
		{
			const int32_t absoluteChannel = ui.channelOffset + j;
			if (absoluteChannel < 0 || absoluteChannel >= song.numChannels ||
				absoluteChannel >= MAX_CHANNELS)
			{
				continue;
			}
			const fastTracksTrackSnapshot_t *fastTrack =
				&fastTracksSnapshot.tracks[absoluteChannel];
			const bool fastTrackVisible = fastTrack->enabled;

			int32_t displayedPattern = currPattern;
			if (fastTrackVisible)
			{
				displayedPattern = fastTrack->sourcePattern;
				if (displayedPattern < 0 || displayedPattern >= MAX_PATTERNS)
					displayedPattern = currPattern;
			}

			uint16_t storedLength = fastTracksPOCGetTrackLength(
				(uint16_t)displayedPattern, absoluteChannel);
			/* Preserve PR42's per-lane visual contract: a LEN OFF lane keeps
			** ordinary FT2 scrolling even while another lane owns the shared LEN
			** boundary. The clutch also disables LEN visuals without erasing the
			** stored value, so bypassed lanes return to normal scrolling. */
			const bool trackLengthVisualActive = lengthTopologyActive &&
				storedLength != 0;
			const bool independentVisual =
				tapeheadTrackUsesIndependentTransportVisual(hybridVisuals,
					fastTrackVisible, trackLengthVisualActive,
					false);

			if (!masterRowValid && !independentVisual)
				continue;

			int32_t playheadRow = -1;
			if (independentVisual && fastTrackVisible)
			{
				playheadRow = fastTrack->sourceRow;
			}
			else if (independentVisual && trackLengthVisualActive)
			{
				playheadRow = fastTracksPOCResolveMasterSourceRow(
					(uint16_t)displayedPattern, absoluteChannel, song.row);
			}
			else if (independentVisual)
			{
				/* Transport Punch freezes the authoritative master row itself. */
				playheadRow = song.row;
			}

			int32_t displayedRow = row;
			if (independentVisual)
			{
				int32_t transportRows;
				if (fastTrackVisible)
				{
					transportRows = fastTracksPOCGetFastTrackLength(
						(uint16_t)displayedPattern, absoluteChannel);
				}
				else if (trackLengthVisualActive)
				{
					transportRows = fastTracksPOCGetEffectiveTrackLength(
						(uint16_t)displayedPattern, absoluteChannel);
				}
				else
				{
					transportRows = fastTracksPOCGetExtendedPatternLength(
						(uint16_t)displayedPattern);
				}

				if (transportRows < 1)
					transportRows = 1;
				if (playheadRow < 0)
					playheadRow = 0;
				if (playheadRow >= transportRows)
					playheadRow %= transportRows;

				/* A private lane never scrolls one row per tick. For patterns taller
				** than the viewport, it changes page only when its real head crosses
				** a page boundary. No duplicate visual counter can drift from audio. */
				const int32_t pageStart = tapeheadTransportVisualPageStart(
					playheadRow, rowsOnScreen);
				displayedRow = pageStart + i;
			}

			const int32_t sourceNumRows = fastTracksPOCGetExtendedPatternLength(
				(uint16_t)displayedPattern);
			const note_t *drawPtr = pattern[displayedPattern] == NULL ||
				displayedRow < 0 || displayedRow >= patternNumRows[displayedPattern] ||
				displayedRow >= sourceNumRows
				? emptyPattern
				: &pattern[displayedPattern]
					[(displayedRow * MAX_CHANNELS) + absoluteChannel];

			uint32_t noteColor = patternFieldColor(0, drawPtr->note != 0);
			uint32_t instColor = patternFieldColor(1, drawPtr->instr != 0);
			uint32_t volColor = patternFieldColor(2, drawPtr->vol >= 0x10);
			uint32_t tuneColor = patternFieldColor(3, drawPtr->tuneType != 0);
			uint32_t efxColor = patternFieldColor(4,
				drawPtr->hasEffect);

			/* Only the row that is logically selected receives selected-row text
			** emphasis. A stationary private lane must not imply that its screen
			** slot is the row an edit cursor would write to. */
			const bool logicalSelectedCell = selectedRowFlag &&
				displayedPattern == currPattern && displayedRow == currRow;
			if (logicalSelectedCell && !patternFieldColorsActive())
				noteColor = instColor = volColor = tuneColor = efxColor = selectedColor;

			if (fastTrackVisible)
			{
				const bool clutchHeld = fastTrack->clutched;
				const uint32_t fastTrackColor = clutchHeld
					? video.palette[PAL_BLCKMRK]
					: video.palette[PAL_BLCKTXT];

				if (drawPtr->note != 0) noteColor = fastTrackColor;
				if (drawPtr->instr != 0) instColor = fastTrackColor;
				if (drawPtr->vol != 0) volColor = fastTrackColor;
				if (drawPtr->hasEffect)
					efxColor = fastTrackColor;
				if (drawPtr->tuneType != 0) tuneColor = fastTrackColor;
			}

			/* Native RGBA selections and literal block markers are supplied at
			** the data boundary, before the original LEN shading and glyph pass. */
			hostCellSelection(absoluteChannel, displayedRow, xPos, textY,
				&noteColor, &instColor, &volColor, &tuneColor, &efxColor);

			const bool lenOwnsFastTrack = lengthTopologyActive && fastTrackVisible &&
				(fastTracksPOCUsesTrackLengths() || fastTrack->clutched);
			const bool inactiveLengthRow = lengthTopologyActive && storedLength != 0 &&
				(!fastTrackVisible || lenOwnsFastTrack) &&
				displayedRow >= fastTracksPOCGetEffectiveTrackLength(
					(uint16_t)displayedPattern, absoluteChannel);
			if (inactiveLengthRow)
			{
				noteColor = dimPatternColor(noteColor);
				instColor = dimPatternColor(instColor);
				volColor = dimPatternColor(volColor);
				tuneColor = dimPatternColor(tuneColor);
				efxColor = dimPatternColor(efxColor);
			}

			drawAdaptiveCell(xPos, textY, drawPtr,
				hostMuteColor(absoluteChannel, noteColor),
				hostMuteColor(absoluteChannel, instColor),
				hostMuteColor(absoluteChannel, volColor),
				hostMuteColor(absoluteChannel, tuneColor),
				hostMuteColor(absoluteChannel, efxColor));

			bool drawPlayhead = independentVisual && displayedRow == playheadRow;
			if (!independentVisual && !hybridVisuals)
			{
				if (songPlaying && fastTrackVisible)
					drawPlayhead = displayedRow == fastTrack->sourceRow;
				else if (songPlaying && storedLength != 0 &&
					currPattern == song.pattNum)
				{
					const int32_t localRow = fastTracksPOCResolveMasterSourceRow(
						(uint16_t)currPattern, absoluteChannel, song.row);
					drawPlayhead = row == localRow;
				}
			}

			if (drawPlayhead)
			{
				const bool hybridFastTracksLength = fastTrackVisible &&
					storedLength != 0 && fastTracksPOCUsesTrackLengths();
				uint32_t playheadColor = fastTrackVisible
					? video.palette[PAL_FASTTRACKS_PLAYHEAD]
					: video.palette[PAL_TRACK_LENGTH_PLAYHEAD];
				if (hybridFastTracksLength)
					playheadColor = video.palette[PAL_FASTTRACKS_LENGTH_PLAYHEAD];
				if (fastTracksPOCGetControlTrack((uint16_t)displayedPattern) ==
					absoluteChannel)
				{
					playheadColor = video.palette[PAL_CONTROL_PLAYHEAD];
				}
				if (lengthTopologyBypassed)
					playheadColor = dimPatternColor(playheadColor);

				drawTrackPlayheadOutline((uint16_t)(xPos + 1),
					(uint16_t)(textY - 1),
					(uint16_t)(ui.patternChannelWidth - 2),
					(uint16_t)rowHeight, playheadColor);
			}
		}

		row++;
		textY += rowHeight;
	}

	writeCursor();

	const uint16_t lengthHeaderY = (uint16_t)(host.layout.len_y - 1);
	drawFastTracksPOCStatus((uint16_t)(host.layout.ratio_y - 1),
		&fastTracksSnapshot);
	drawTrackLengthStatus(lengthHeaderY, (uint16_t)currPattern);

	if (config.ptnChnNumbers)
		drawChannelNumbering(lengthHeaderY);

}

void pattTwoHexOut(uint32_t xPos, uint32_t yPos, uint8_t val, uint32_t color)
{
	const uint8_t *ch1Ptr = &font4Ptr[(val   >> 4) * FONT4_CHAR_W];
	const uint8_t *ch2Ptr = &font4Ptr[(val & 0x0F) * FONT4_CHAR_W];
	uint32_t *dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	for (int32_t y = 0; y < FONT4_CHAR_H; y++)
	{
		for (int32_t x = 0; x < FONT4_CHAR_W; x++)
		{
			if (ch1Ptr[x] != 0) dstPtr[x] = color;
			if (ch2Ptr[x] != 0) dstPtr[FONT4_CHAR_W+x] = color;
		}

		ch1Ptr += FONT4_WIDTH;
		ch2Ptr += FONT4_WIDTH;
		dstPtr += SCREEN_W;
	}
}

static void pattCharOut(uint32_t xPos, uint32_t yPos, uint8_t chr, uint8_t fontType, uint32_t color)
{
	const uint8_t *srcPtr;
	int32_t x, y;

	uint32_t *dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	if (fontType == FONT_TYPE3)
	{
		srcPtr = &bmp.font3[chr * FONT3_CHAR_W];
		for (y = 0; y < FONT3_CHAR_H; y++)
		{
			for (x = 0; x < FONT3_CHAR_W; x++)
			{
				if (srcPtr[x] != 0)
					dstPtr[x] = color;
			}

			srcPtr += FONT3_WIDTH;
			dstPtr += SCREEN_W;
		}
	}
	else if (fontType == FONT_TYPE4)
	{
		srcPtr = &font4Ptr[chr * FONT4_CHAR_W];
		for (y = 0; y < FONT4_CHAR_H; y++)
		{
			for (x = 0; x < FONT4_CHAR_W; x++)
			{
				if (srcPtr[x] != 0)
					dstPtr[x] = color;
			}

			srcPtr += FONT4_WIDTH;
			dstPtr += SCREEN_W;
		}
	}
}

static void drawEmptyNoteMedium(uint32_t xPos, uint32_t yPos, uint32_t color)
{
	uint32_t *dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	if (config.ptnAlternativeLayout)
	{
		dstPtr += (3 * SCREEN_W) + 3;
		*dstPtr = color;
		dstPtr += 8;
		*dstPtr = color;
		dstPtr += 8;
		*dstPtr = color;
	}
	else
	{
		const uint8_t *srcPtr = &font4Ptr[43 * FONT4_CHAR_W];

		for (int32_t y = 0; y < FONT4_CHAR_H; y++)
		{
			for (int32_t x = 0; x < FONT4_CHAR_W*3; x++)
			{
				if (srcPtr[x] != 0)
					dstPtr[x] = color;
			}

			srcPtr += FONT4_WIDTH;
			dstPtr += SCREEN_W;
		}
	}
}

static void drawKeyOffMedium(uint32_t xPos, uint32_t yPos, uint32_t color)
{
	const uint8_t *srcPtr = &font4Ptr[40 * FONT4_CHAR_W];
	uint32_t *dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	for (int32_t y = 0; y < FONT4_CHAR_H; y++)
	{
		for (int32_t x = 0; x < FONT4_CHAR_W*3; x++)
		{
			if (srcPtr[x] != 0)
				dstPtr[x] = color;
		}

		srcPtr += FONT4_WIDTH;
		dstPtr += SCREEN_W;
	}
}

static void drawNoteMedium(uint32_t xPos, uint32_t yPos, int32_t noteNum, uint32_t color)
{
	uint32_t char1, char2;

	noteNum--;

	const uint8_t note = noteTab1[noteNum];
	const uint32_t char3 = noteTab2[noteNum] * FONT4_CHAR_W;

	if (config.ptnAcc == 0)
	{
		char1 = sharpNote1Char_med[note];
		char2 = sharpNote2Char_med[note];
	}
	else
	{
		char1 = flatNote1Char_med[note];
		char2 = flatNote2Char_med[note];
	}

	const uint8_t *ch1Ptr = &font4Ptr[char1];
	const uint8_t *ch2Ptr = &font4Ptr[char2];
	const uint8_t *ch3Ptr = &font4Ptr[char3];
	uint32_t *dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	for (int32_t y = 0; y < FONT4_CHAR_H; y++)
	{
		for (int32_t x = 0; x < FONT4_CHAR_W; x++)
		{
			if (ch1Ptr[x] != 0) dstPtr[x] = color;
			if (ch2Ptr[x] != 0) dstPtr[FONT4_CHAR_W+x] = color;
			if (ch3Ptr[x] != 0) dstPtr[(FONT4_CHAR_W*2)+x] = color;
		}

		ch1Ptr += FONT4_WIDTH;
		ch2Ptr += FONT4_WIDTH;
		ch3Ptr += FONT4_WIDTH;
		dstPtr += SCREEN_W;
	}
}
