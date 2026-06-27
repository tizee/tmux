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

#ifndef CAPTURE_HINT_H
#define CAPTURE_HINT_H

/* Default hint key set (vim homerow). */
#define CAPTURE_DEFAULT_HINT_KEYS "asdfjklgh"

/* Maximum hint label length. K^4 hints is far more than a screen can hold. */
#define CAPTURE_HINT_MAX_LEN 4

/* A single hint label. */
struct capture_hint {
	char	*label;		/* e.g. "a", "gh", "jkl" */
	int	 len;		/* length of label (1..CAPTURE_HINT_MAX_LEN) */
};

/*
 * Generate prefix-free hint labels for num_hints matches.
 * Uses the key set from keys (e.g., "asdfjklgh").
 *
 * Labels use the shortest possible prefix-free distribution: with K keys the
 * minimum length L satisfies K^(L-1) < num_hints <= K^L, and as many length
 * L-1 labels as possible are used before reserving prefixes for length L
 * labels. Labels may therefore have mixed lengths (e.g., "a" and "jk").
 *
 * Returns a malloc'd array, sets *count to num_hints on success. Returns NULL
 * (and sets *count to 0) only on bad input or if num_hints exceeds the
 * representable capacity K^CAPTURE_HINT_MAX_LEN.
 */
struct capture_hint	*capture_hint_generate(int, const char *, int *);
void			 capture_hint_free(struct capture_hint *, int);

#endif /* CAPTURE_HINT_H */
