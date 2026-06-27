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

#ifndef CAPTURE_PATTERN_H
#define CAPTURE_PATTERN_H

#include <regex.h>
#include <sys/types.h>

/*
 * Built-in default pattern for capture mode. Matches URLs, emails, git SHAs,
 * file paths, IP addresses, UUIDs, hex colors, MAC addresses, hashes.
 *
 * The @capture-patterns user option appends additional alternations.
 * The @capture-patterns-override user option replaces this entirely.
 */
#define CAPTURE_DEFAULT_PATTERNS \
	"https?://[^[:space:]]+" \
	"|[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}" \
	"|[0-9a-fA-F]{7,40}" \
	"|(~|\\.\\.?)?(/[[:alnum:]_.@$:()~+-]+)+" \
	"|[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}" \
	"|[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}" \
	"-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}" \
	"|#[0-9a-fA-F]{8}[[:>:]]" \
	"|#[0-9a-fA-F]{6}[[:>:]]" \
	"|#[0-9a-fA-F]{3}[[:>:]]" \
	"|([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}" \
	"|sha256:[0-9a-fA-F]{64}" \
	"|0x[0-9a-fA-F]+"

/* A compiled pattern (single regex with alternations). */
struct capture_pattern {
	regex_t	 re;
	int	 valid;		/* 1 if compiled successfully */
};

/* A single match result from pattern scanning. */
struct capture_match {
	u_int	 sx, sy;	/* start column, row */
	u_int	 ex, ey;	/* end column, row (exclusive) */
	char	*text;		/* matched text (caller must free) */
};

/* Compile a regex pattern string. Returns 0 on success, -1 on error. */
int			 capture_pattern_compile(struct capture_pattern *,
			     const char *);
void			 capture_pattern_free(struct capture_pattern *);

/* Match pattern against a single line of text. Returns malloc'd array. */
struct capture_match	*capture_pattern_match_line(struct capture_pattern *,
			     const char *, u_int, int *);

/* Resolve overlapping matches using length-based rule. */
void			 capture_pattern_resolve_overlaps(struct capture_match *,
			     int *);

/* Free a match array. */
void			 capture_match_free(struct capture_match *, int);

#endif /* CAPTURE_PATTERN_H */
