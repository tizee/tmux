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

#include <regex.h>
#include <stdlib.h>
#include <string.h>

#include "capture-pattern.h"

/*
 * Compile a regex pattern string (with | alternations).
 * Returns 0 on success, -1 on error.
 */
int capture_pattern_compile(struct capture_pattern *cp, const char *pattern) {
  int rc;
  char errbuf[256];

  if (pattern == NULL || *pattern == '\0') {
    cp->valid = 0;
    return (0);
  }

  rc = regcomp(&cp->re, pattern, REG_EXTENDED);
  if (rc != 0) {
    regerror(rc, &cp->re, errbuf, sizeof errbuf);
    cp->valid = 0;
    return (-1);
  }

  cp->valid = 1;
  return (0);
}

void capture_pattern_free(struct capture_pattern *cp) {
  if (cp->valid)
    regfree(&cp->re);
  cp->valid = 0;
}

/*
 * Match a compiled pattern against a single line of text.
 * Returns an array of matches, sets *count.
 */
struct capture_match *capture_pattern_match_line(struct capture_pattern *cp,
                                                 const char *line,
                                                 u_int line_no, int *count) {
  struct capture_match *matches = NULL;
  int nmatches = 0;
  regmatch_t pmatch;
  size_t offset, len;

  if (!cp->valid || line == NULL) {
    *count = 0;
    return (NULL);
  }

  len = strlen(line);
  offset = 0;
  while (offset < len && regexec(&cp->re, line + offset, 1, &pmatch,
                                 offset > 0 ? REG_NOTBOL : 0) == 0) {
    struct capture_match *new_matches;
    struct capture_match *m;
    regoff_t so, eo;

    so = pmatch.rm_so;
    eo = pmatch.rm_eo;

    /* Skip zero-length matches to avoid infinite loop */
    if (so == eo) {
      offset += 1;
      if (offset >= len)
        break;
      continue;
    }

    new_matches = realloc(matches, (size_t)(nmatches + 1) * sizeof *matches);
    if (new_matches == NULL)
      goto error;
    matches = new_matches;

    m = &matches[nmatches];
    m->sx = (u_int)(offset + (size_t)so);
    m->sy = line_no;
    m->ex = (u_int)(offset + (size_t)eo);
    m->ey = line_no;
    m->text = strndup(line + offset + (size_t)so, (size_t)(eo - so));
    if (m->text == NULL)
      goto error;

    nmatches++;
    offset += (size_t)eo;
  }

  *count = nmatches;
  return (matches);

error:
  capture_match_free(matches, nmatches);
  *count = 0;
  return (NULL);
}

/*
 * Check if two matches overlap (same line, column ranges intersect).
 */
static int capture_match_overlaps(const struct capture_match *a,
                                  const struct capture_match *b) {
  if (a->sy != b->sy)
    return (0);
  if (a->ex <= b->sx || b->ex <= a->sx)
    return (0);
  return (1);
}

/*
 * Get the length of a match in columns.
 */
static u_int capture_match_length(const struct capture_match *m) {
  return (m->ex - m->sx);
}

/*
 * Resolve overlapping matches.
 * Rule: longer match wins. If same length, earlier match wins.
 * Overlapping losers have their text set to NULL, then the array is compacted.
 */
void capture_pattern_resolve_overlaps(struct capture_match *matches,
                                      int *count) {
  int i, j, n;

  n = *count;
  for (i = 0; i < n; i++) {
    if (matches[i].text == NULL)
      continue;
    for (j = i + 1; j < n; j++) {
      if (matches[j].text == NULL)
        continue;
      if (!capture_match_overlaps(&matches[i], &matches[j]))
        continue;

      if (capture_match_length(&matches[i]) <
          capture_match_length(&matches[j])) {
        free(matches[i].text);
        matches[i].text = NULL;
        break; /* i lost, move to next i */
      } else {
        /*
         * Same length or longer: earlier match
         * (lower index) wins.
         */
        free(matches[j].text);
        matches[j].text = NULL;
      }
    }
  }

  /* Compact array, removing NULL entries */
  j = 0;
  for (i = 0; i < n; i++) {
    if (matches[i].text != NULL) {
      if (i != j)
        matches[j] = matches[i];
      j++;
    }
  }
  *count = j;
}

void capture_match_free(struct capture_match *matches, int count) {
  int i;

  if (matches == NULL)
    return;

  for (i = 0; i < count; i++)
    free(matches[i].text);
  free(matches);
}
