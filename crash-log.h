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

#ifndef CRASH_LOG_H
#define CRASH_LOG_H

/*
 * Ring-buffer crash logging (tizee fork, --enable-crash-log).
 *
 * crash_log_record() copies the most recent formatted log lines into a fixed
 * in-memory ring. On a fatal signal an async-signal-safe handler writes the
 * backtrace and the tail of that ring to <dir>/tmux-crash-<pid>.log, then
 * re-raises the signal so the OS crash reporter still runs. The point is that
 * even a release build in daily use leaves a locatable trail after a crash.
 */

/* Number of recent log lines retained, and the max bytes stored per line. */
#define CRASH_RING_LINES 512
#define CRASH_RING_LINE  256

/* Install the fatal-signal handler; crash files are written under dir. */
void	crash_log_init(const char *dir);

/* Record one already-formatted log line into the ring (cheap, no malloc). */
void	crash_log_record(const char *line);

#endif /* CRASH_LOG_H */
