/* $OpenBSD$ */

/*
 * Copyright (c) 2025 tizee <tizee@github.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "version-format.h"

void
version_format_short(char *buf, size_t len, const char *base,
    const char *git_hash)
{
	if (git_hash != NULL && *git_hash != '\0')
		snprintf(buf, len, "%s+g%s", base, git_hash);
	else
		snprintf(buf, len, "%s", base);
}

void
version_format_relative(char *buf, size_t len, time_t build, time_t now)
{
	long		 delta = (long)(now - build);
	struct tm	 tm;

	if (delta < 60) {
		snprintf(buf, len, "just now");
		return;
	}
	if (delta < 3600) {
		long	minutes = delta / 60;

		if (minutes > 1)
			snprintf(buf, len, "%ld min ago", minutes);
		else
			snprintf(buf, len, "1 min ago");
		return;
	}
	if (delta < 86400) {
		long	hours = delta / 3600;

		if (hours > 1)
			snprintf(buf, len, "%ld hours ago", hours);
		else
			snprintf(buf, len, "1 hour ago");
		return;
	}
	if (delta < 604800) {
		long	days = delta / 86400;

		if (days == 1)
			snprintf(buf, len, "yesterday");
		else
			snprintf(buf, len, "%ld days ago", days);
		return;
	}

	gmtime_r(&build, &tm);
	strftime(buf, len, "%Y-%m-%d", &tm);
}

void
version_format_full(char *buf, size_t len, const char *base,
    const char *git_hash, time_t build, time_t now)
{
	char		 shortver[128];
	char		 iso[32];
	char		 rel[64];
	struct tm	 tm;

	version_format_short(shortver, sizeof shortver, base, git_hash);

	if (git_hash == NULL || *git_hash == '\0' || build <= 0) {
		snprintf(buf, len, "%s", shortver);
		return;
	}

	gmtime_r(&build, &tm);
	strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%SZ", &tm);
	version_format_relative(rel, sizeof rel, build, now);

	snprintf(buf, len, "%s (released %s, %s)", shortver, iso, rel);
}
