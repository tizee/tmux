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

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tmux.h"
#include "crash-log.h"

#define CRASH_ALT_STACK	 (128 * 1024)
#define CRASH_KEEP_FILES 20
#define CRASH_PATH_MAX	 4096

/*
 * The ring is written by the (single-threaded) server whenever it logs, and
 * read from the signal handler. A signal can interrupt a half-written slot;
 * that is acceptable — the ring is a best-effort trail, not a transaction log.
 */
static char			crash_ring[CRASH_RING_LINES][CRASH_RING_LINE];
static volatile unsigned	crash_ring_head;

static char			crash_path[CRASH_PATH_MAX];  /* built at init */
static char			crash_altstack[CRASH_ALT_STACK];
static volatile sig_atomic_t	crash_in_handler;

void
crash_log_record(const char *line)
{
	unsigned	slot = crash_ring_head % CRASH_RING_LINES;

	strlcpy(crash_ring[slot], line, CRASH_RING_LINE);
	crash_ring_head++;
}

/* Async-signal-safe unsigned -> decimal. Returns the written length. */
static size_t
crash_utoa(unsigned long v, char *buf, size_t len)
{
	char	tmp[24];
	size_t	i = 0, j = 0;

	if (len == 0)
		return (0);
	do {
		tmp[i++] = '0' + (char)(v % 10);
		v /= 10;
	} while (v != 0 && i < sizeof tmp);
	while (i > 0 && j < len - 1)
		buf[j++] = tmp[--i];
	buf[j] = '\0';
	return (j);
}

/* Async-signal-safe: write a NUL-terminated string. */
static void
crash_puts(int fd, const char *s)
{
	(void)write(fd, s, strlen(s));
}

/* Async-signal-safe: no strsignal (not signal-safe). */
static const char *
crash_signame(int sig)
{
	switch (sig) {
	case SIGSEGV:
		return ("SIGSEGV");
	case SIGBUS:
		return ("SIGBUS");
	case SIGABRT:
		return ("SIGABRT");
	case SIGILL:
		return ("SIGILL");
	case SIGFPE:
		return ("SIGFPE");
	default:
		return ("signal");
	}
}

static void
crash_handler(int sig)
{
	void		*bt[64];
	int		 fd, n;
	unsigned	 head, start, i;
	char		 num[24];

	/* If we fault again while handling, do not loop. */
	if (crash_in_handler)
		_exit(128 + sig);
	crash_in_handler = 1;

	fd = open(crash_path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
	if (fd != -1) {
		crash_puts(fd, "=== tmux crash ===\nsignal: ");
		crash_puts(fd, crash_signame(sig));
		crash_puts(fd, " (");
		crash_utoa((unsigned long)sig, num, sizeof num);
		crash_puts(fd, num);
		crash_puts(fd, ")\npid: ");
		crash_utoa((unsigned long)getpid(), num, sizeof num);
		crash_puts(fd, num);

		crash_puts(fd, "\n--- backtrace ---\n");
		n = backtrace(bt, 64);
		backtrace_symbols_fd(bt, n, fd);

		crash_puts(fd, "--- recent log ring (oldest first) ---\n");
		head = crash_ring_head;
		start = (head > CRASH_RING_LINES) ? head - CRASH_RING_LINES : 0;
		for (i = start; i < head; i++) {
			const char	*l = crash_ring[i % CRASH_RING_LINES];

			if (l[0] != '\0') {
				crash_puts(fd, l);
				(void)write(fd, "\n", 1);
			}
		}
		crash_puts(fd, "=== end ===\n");
		(void)close(fd);
	}

	/* Restore the default and re-raise so the OS crash reporter runs too. */
	signal(sig, SIG_DFL);
	raise(sig);
}

/*
 * Keep only the newest CRASH_KEEP_FILES crash files. Runs at init time on the
 * normal path, so ordinary libc calls are fine here (unlike the handler).
 */
static void
crash_prune(const char *dir)
{
	DIR		*d;
	struct dirent	*de;
	struct stat	 st;
	struct {
		char	name[256];
		time_t	mtime;
	}		 ents[128];
	unsigned	 n = 0, i, j, oldest;
	char		 path[CRASH_PATH_MAX];

	d = opendir(dir);
	if (d == NULL)
		return;
	while ((de = readdir(d)) != NULL && n < nitems(ents)) {
		if (strncmp(de->d_name, "tmux-crash-", 11) != 0)
			continue;
		xsnprintf(path, sizeof path, "%s/%s", dir, de->d_name);
		if (stat(path, &st) != 0)
			continue;
		strlcpy(ents[n].name, de->d_name, sizeof ents[n].name);
		ents[n].mtime = st.st_mtime;
		n++;
	}
	closedir(d);

	while (n > CRASH_KEEP_FILES) {
		oldest = 0;
		for (i = 1; i < n; i++) {
			if (ents[i].mtime < ents[oldest].mtime)
				oldest = i;
		}
		xsnprintf(path, sizeof path, "%s/%s", dir, ents[oldest].name);
		(void)unlink(path);
		for (j = oldest; j + 1 < n; j++)
			ents[j] = ents[j + 1];
		n--;
	}
}

void
crash_log_init(const char *dir)
{
	static const int	sigs[] = {
		SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE
	};
	struct sigaction	sa;
	stack_t			ss;
	char			num[24];
	unsigned		i;

	if (dir == NULL || *dir == '\0')
		dir = "/tmp";
	(void)mkdir(dir, 0700);

	/* Precompute the crash file path: <dir>/tmux-crash-<pid>.log */
	strlcpy(crash_path, dir, sizeof crash_path);
	strlcat(crash_path, "/tmux-crash-", sizeof crash_path);
	crash_utoa((unsigned long)getpid(), num, sizeof num);
	strlcat(crash_path, num, sizeof crash_path);
	strlcat(crash_path, ".log", sizeof crash_path);

	crash_prune(dir);

	/* Alternate stack so a stack-overflow SIGSEGV can still be handled. */
	ss.ss_sp = crash_altstack;
	ss.ss_size = sizeof crash_altstack;
	ss.ss_flags = 0;
	(void)sigaltstack(&ss, NULL);

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = crash_handler;
	sa.sa_flags = SA_ONSTACK | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	for (i = 0; i < nitems(sigs); i++)
		(void)sigaction(sigs[i], &sa, NULL);

	log_debug("crash log armed: %s", crash_path);
}
