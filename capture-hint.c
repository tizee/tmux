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

#include <stdlib.h>
#include <string.h>

#include "capture-hint.h"

/*
 * Decode a base-nkeys index into a fixed-length label of length len.
 * The most significant digit is written first so labels are ordered
 * lexicographically. The buffer must hold len + 1 bytes.
 */
static void
capture_hint_decode(char *buf, unsigned long index, int len, const char *keys,
    int nkeys)
{
	int i;

	for (i = len - 1; i >= 0; i--) {
		buf[i] = keys[index % (unsigned long)nkeys];
		index /= (unsigned long)nkeys;
	}
	buf[len] = '\0';
}

/*
 * Generate prefix-free hint labels with the optimal (shortest possible)
 * length distribution.
 *
 * For an alphabet of size a and required count n, the minimum maximum length
 * L satisfies a^(L-1) < n <= a^L. To minimise label length while staying
 * prefix-free, use as many length-(L-1) labels as possible and reserve the
 * remaining length-(L-1) strings as prefixes for length-L labels:
 *
 *	n_short = floor((a^L - n) / (a - 1))
 *	n_long  = n - n_short
 *
 * Length-(L-1) strings [0, n_short) become short labels; the remaining
 * strings [n_short, a^(L-1)) are extended by every alphabet character to
 * form the long labels. Short labels are never prefixes of long labels
 * because their generating strings are disjoint.
 */
struct capture_hint *
capture_hint_generate(int num_hints, const char *keys, int *count)
{
	struct capture_hint	*hints;
	unsigned long		 cap, i, idx, prefix;
	int			 nkeys, len, short_len, n_short, j;
	char			 label[CAPTURE_HINT_MAX_LEN + 1];

	if (num_hints <= 0 || keys == NULL || *keys == '\0') {
		*count = 0;
		return (NULL);
	}

	nkeys = (int)strlen(keys);
	if (nkeys < 2) {
		/* A single key cannot produce prefix-free multi-char hints. */
		*count = 0;
		return (NULL);
	}

	/* Find the minimum length L such that nkeys^L >= num_hints. */
	len = 1;
	cap = (unsigned long)nkeys;
	while (cap < (unsigned long)num_hints) {
		if (len >= CAPTURE_HINT_MAX_LEN) {
			*count = 0;
			return (NULL);
		}
		cap *= (unsigned long)nkeys;
		len++;
	}

	hints = calloc((size_t)num_hints, sizeof *hints);
	if (hints == NULL) {
		*count = 0;
		return (NULL);
	}

	if (len == 1) {
		/* All hints fit in a single character. */
		for (j = 0; j < num_hints; j++) {
			hints[j].label = malloc(2);
			if (hints[j].label == NULL)
				goto error;
			hints[j].label[0] = keys[j];
			hints[j].label[1] = '\0';
			hints[j].len = 1;
		}
		*count = num_hints;
		return (hints);
	}

	/*
	 * cap == nkeys^len here. Compute the split between short labels of
	 * length len-1 and long labels of length len.
	 */
	short_len = len - 1;
	n_short = (int)((cap - (unsigned long)num_hints) /
	    (unsigned long)(nkeys - 1));

	j = 0;

	/* Short labels: first n_short strings of length len-1. */
	for (i = 0; i < (unsigned long)n_short; i++) {
		capture_hint_decode(label, i, short_len, keys, nkeys);
		hints[j].label = strdup(label);
		if (hints[j].label == NULL)
			goto error;
		hints[j].len = short_len;
		j++;
	}

	/*
	 * Long labels: remaining length-(len-1) strings act as prefixes, each
	 * extended by every alphabet character until num_hints is reached.
	 */
	prefix = (unsigned long)n_short;
	while (j < num_hints) {
		capture_hint_decode(label, prefix, short_len, keys, nkeys);
		for (idx = 0; idx < (unsigned long)nkeys && j < num_hints;
		    idx++) {
			label[short_len] = keys[idx];
			label[short_len + 1] = '\0';
			hints[j].label = strdup(label);
			if (hints[j].label == NULL)
				goto error;
			hints[j].len = len;
			j++;
		}
		prefix++;
	}

	*count = num_hints;
	return (hints);

error:
	capture_hint_free(hints, j);
	*count = 0;
	return (NULL);
}

void
capture_hint_free(struct capture_hint *hints, int count)
{
	int i;

	if (hints == NULL)
		return;

	for (i = 0; i < count; i++)
		free(hints[i].label);
	free(hints);
}
