/*
 * ascpu is the CPU statistics monitor utility for X Windows
 * Copyright (c) 1998-2000  Albert Dorofeev <albert@tigr.net>
 * For the updates see http://www.tigr.net/
 *
 * This software is distributed under GPL. For details see LICENSE file.
 */

#include <string.h>

/*
 * Copies at most maxlen-1 characters from the source.
 * Makes sure that the destination string is zero-terminated.
 */
char *safecopy(char *dest, const char *src, unsigned short maxlen)
{
	/* safety precaution */
	dest[maxlen-1] = 0;
	return strlen(src) < maxlen ? strcpy(dest, src) : strncpy(dest, src, maxlen-1);
}

