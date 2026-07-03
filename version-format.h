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

#ifndef VERSION_FORMAT_H
#define VERSION_FORMAT_H

#include <sys/types.h>
#include <time.h>

/*
 * Pure version-string formatting for local git-aware builds. Libc only (no
 * tmux runtime) so it can be unit-tested standalone. All outputs are written
 * into caller-provided fixed buffers and are always NUL-terminated.
 */

/*
 * Compose the compact display version into buf: "base" when git_hash is empty,
 * otherwise "base+ghash".
 */
void	version_format_short(char *buf, size_t len, const char *base,
	    const char *git_hash);

/*
 * Human-readable relative time from build to now (UTC buckets, ported from the
 * zhu agent):
 *   < 60s   -> "just now" (also covers negative/clock-skew deltas)
 *   < 1h    -> "N min ago" ("1 min ago" for one)
 *   < 24h   -> "N hours ago" ("1 hour ago" for one)
 *   < 7d    -> "yesterday" (one day) or "N days ago"
 *   else    -> the build date as "YYYY-MM-DD"
 */
void	version_format_relative(char *buf, size_t len, time_t build,
	    time_t now);

/*
 * Compose the rich version line into buf. When git_hash is empty or build is
 * not positive, this equals version_format_short(). Otherwise:
 *   "base+ghash (released <iso-utc>, <relative>)"
 * where iso-utc is "YYYY-MM-DDTHH:MM:SSZ" derived from build.
 */
void	version_format_full(char *buf, size_t len, const char *base,
	    const char *git_hash, time_t build, time_t now);

#endif /* VERSION_FORMAT_H */
