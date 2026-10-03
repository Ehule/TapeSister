void textOutTiny(int32_t xPos, int32_t yPos, char *str, uint32_t color) // A..Z/a..z and 0..9
{
	uint32_t *dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];
	while (*str != '\0')
	{
		char chr = *str++;

		if (chr >= '0' && chr <= '9')
		{
			chr -= '0';
		}
		else if (chr >= 'a' && chr <= 'z')
		{
			chr -= 'a';
			chr += 10;
		}
		else if (chr >= 'A' && chr <= 'Z')
		{
			chr -= 'A';
			chr += 10;
		}
		else
		{
			dstPtr += FONT3_CHAR_W;
			continue;
		}

		const uint8_t *srcPtr = &bmp.font3[chr * FONT3_CHAR_W];
		for (int32_t y = 0; y < FONT3_CHAR_H; y++)
		{
			for (int32_t x = 0; x < FONT3_CHAR_W; x++)
			{
				if (srcPtr[x] != 0)
					dstPtr[x] = color;
			}

			srcPtr += FONT3_WIDTH;
			dstPtr += SCREEN_W;
		}

		dstPtr -= (SCREEN_W * FONT3_CHAR_H) - FONT3_CHAR_W;
	}
}

static inline void tapeheadTextOutTinyWithColon(int32_t xPos, int32_t yPos,
	char *str, uint32_t color)
{
	textOutTiny(xPos, yPos, str, color);

	for (int32_t i = 0; str[i] != '\0'; i++)
	{
		if (str[i] != ':')
			continue;

		const int32_t colonX = xPos + (i * FONT3_CHAR_W) + 1;
		video.frameBuffer[((yPos + 2) * SCREEN_W) + colonX] = color;
		video.frameBuffer[((yPos + 4) * SCREEN_W) + colonX] = color;
	}
}

#define textOutTiny tapeheadTextOutTinyWithColon
