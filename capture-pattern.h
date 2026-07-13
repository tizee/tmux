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
 * Built-in default patterns for capture mode, one entry per named category:
 * URLs, emails, git SHAs, file paths, IPv4, UUIDs, hex colors, MAC addresses,
 * sha256 hashes and hex addresses.
 *
 * Category names (for @capture-patterns-disable): url, email, git-sha, path,
 * ipv4, uuid, hex-color, mac, sha256, hex-address.
 *
 * The @capture-patterns user option appends additional alternations.
 * The @capture-patterns-disable option removes named categories (comma list).
 * The @capture-patterns-override option replaces this entirely.
 */
#define CAPTURE_PAT_URL		"https?://[^[:space:]]+"
#define CAPTURE_PAT_EMAIL	"[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}"
#define CAPTURE_PAT_GIT_SHA	"[0-9a-fA-F]{7,40}"
#define CAPTURE_PAT_PATH	"[[:alnum:]_.@$:()~+-]*(/[[:alnum:]_.@$:()~+-]+)+"
#define CAPTURE_PAT_IPV4 \
	"[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}"
#define CAPTURE_PAT_UUID \
	"[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}" \
	"-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}"
#define CAPTURE_PAT_HEX_COLOR \
	"#[0-9a-fA-F]{8}[[:>:]]|#[0-9a-fA-F]{6}[[:>:]]|#[0-9a-fA-F]{3}[[:>:]]"
#define CAPTURE_PAT_MAC		"([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}"
#define CAPTURE_PAT_SHA256	"sha256:[0-9a-fA-F]{64}"
#define CAPTURE_PAT_HEX_ADDRESS	"0x[0-9a-fA-F]+"

#define CAPTURE_DEFAULT_PATTERNS \
	CAPTURE_PAT_URL "|" CAPTURE_PAT_EMAIL "|" CAPTURE_PAT_GIT_SHA "|" \
	CAPTURE_PAT_PATH "|" CAPTURE_PAT_IPV4 "|" CAPTURE_PAT_UUID "|" \
	CAPTURE_PAT_HEX_COLOR "|" CAPTURE_PAT_MAC "|" CAPTURE_PAT_SHA256 "|" \
	CAPTURE_PAT_HEX_ADDRESS

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

/*
 * Build the default pattern string, excluding categories named in the
 * comma-separated list (NULL or "" disables nothing; unknown names are
 * ignored). Returns a malloc'd string (possibly empty); caller frees.
 */
char			*capture_pattern_default(const char *);

/* Match pattern against a single line of text. Returns malloc'd array. */
struct capture_match	*capture_pattern_match_line(struct capture_pattern *,
			     const char *, u_int, int *);

/* Resolve overlapping matches using length-based rule. */
void			 capture_pattern_resolve_overlaps(struct capture_match *,
			     int *);

/* Free a match array. */
void			 capture_match_free(struct capture_match *, int);

#endif /* CAPTURE_PATTERN_H */
