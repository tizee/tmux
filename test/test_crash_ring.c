/*
 * Test harness for the crash ring buffer noise filter (crash-log.c).
 *
 * Compile standalone (needs libevent headers for tmux.h; the HAVE_* defines
 * silence compat.h fallbacks that conflict with macOS libc):
 *   cc -Wall -DHAVE_STRLCPY=1 -DHAVE_STRLCAT=1 -DHAVE_CLOCK_GETTIME=1 \
 *     -DHAVE_FLOCK=1 -I. -I/opt/homebrew/opt/libevent/include \
 *     -o /tmp/test_crash_ring test/test_crash_ring.c crash-log.c
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../crash-log.h"

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) \
	do { \
		tests_run++; \
		printf("  %s ... ", #name); \
		test_##name(); \
		printf("ok\n"); \
	} while (0)

#define ASSERT(cond, msg) \
	do { \
		if (!(cond)) { \
			printf("FAIL: %s\n", msg); \
			tests_failed++; \
			return; \
		} \
	} while (0)

/* Stubs: crash-log.c references these on paths the tests never run. */
void
log_debug(const char *msg, ...)
{
	(void)msg;
}

int
xsnprintf(char *str, size_t len, const char *fmt, ...)
{
	va_list	ap;
	int	n;

	va_start(ap, fmt);
	n = vsnprintf(str, len, fmt, ap);
	va_end(ap);
	return (n);
}

/* Return the ring line at absolute index base+off. */
static const char *
line_at(unsigned idx)
{
	return (crash_log_ring_line(idx));
}

TEST(normal_lines_kept)
{
	unsigned	base = crash_log_ring_head();

	crash_log_record("cmdq_next <global>: empty");
	crash_log_record("server_client_check_pane_buffer: pane %4 is on");

	ASSERT(crash_log_ring_head() == base + 2, "two lines recorded");
	ASSERT(strcmp(line_at(base), "cmdq_next <global>: empty") == 0,
	    "first line kept verbatim");
	ASSERT(strcmp(line_at(base + 1),
	    "server_client_check_pane_buffer: pane %4 is on") == 0,
	    "second line kept verbatim");
}

TEST(noise_lines_dropped)
{
	unsigned	base = crash_log_ring_head();

	crash_log_record("utf8_to_data: 438094e2 -> (1 3 X)");
	crash_log_record("utf8_from_data: (1 3 X) -> 438094e2");
	crash_log_record("UTF-8 X is U+002500");
	crash_log_record("utf8proc_wcwidth(02500) returned 1");
	crash_log_record("input_top_bit_set 3 'X' (width 1)");
	crash_log_record("screen_write_combine: character X at 67,43 (width 1)");

	ASSERT(crash_log_ring_head() == base, "no slot consumed by noise");
}

TEST(noise_run_folded_into_summary)
{
	unsigned	base;
	int		i;

	/* Flush any dropped count pending from the previous test. */
	crash_log_record("sync line");
	base = crash_log_ring_head();

	for (i = 0; i < 647; i++)
		crash_log_record("utf8_to_data: spam");
	crash_log_record("window_copy_clone_screen: target screen is 177x1063");

	ASSERT(crash_log_ring_head() == base + 2,
	    "summary plus kept line recorded");
	ASSERT(strcmp(line_at(base),
	    "(ring: dropped 647 render-noise lines)") == 0,
	    "summary line counts the dropped run");
	ASSERT(strcmp(line_at(base + 1),
	    "window_copy_clone_screen: target screen is 177x1063") == 0,
	    "kept line follows the summary");
}

TEST(no_summary_without_noise)
{
	unsigned	base = crash_log_ring_head();

	crash_log_record("cmd_find_target: wp=%58");
	crash_log_record("cmd_find_target: idx=none");

	ASSERT(crash_log_ring_head() == base + 2,
	    "no spurious summary between normal lines");
	ASSERT(strncmp(line_at(base), "(ring:", 6) != 0,
	    "first slot is not a summary");
	ASSERT(strncmp(line_at(base + 1), "(ring:", 6) != 0,
	    "second slot is not a summary");
}

TEST(prefix_match_only_at_line_start)
{
	unsigned	base = crash_log_ring_head();

	/* A normal line that merely mentions a noisy function is kept. */
	crash_log_record("message: key X: utf8_to_data trace enabled");

	ASSERT(crash_log_ring_head() == base + 1,
	    "substring elsewhere in the line does not match");
	ASSERT(strcmp(line_at(base),
	    "message: key X: utf8_to_data trace enabled") == 0,
	    "line kept verbatim");
}

TEST(long_line_truncated_not_overflowed)
{
	unsigned	base = crash_log_ring_head();
	char		big[CRASH_RING_LINE * 2];

	memset(big, 'A', sizeof big - 1);
	big[sizeof big - 1] = '\0';
	crash_log_record(big);

	ASSERT(crash_log_ring_head() == base + 1, "long line recorded");
	ASSERT(strlen(line_at(base)) == CRASH_RING_LINE - 1,
	    "line truncated to slot size");
}

int
main(void)
{
	printf("test_crash_ring:\n");

	RUN_TEST(normal_lines_kept);
	RUN_TEST(noise_lines_dropped);
	RUN_TEST(noise_run_folded_into_summary);
	RUN_TEST(no_summary_without_noise);
	RUN_TEST(prefix_match_only_at_line_start);
	RUN_TEST(long_line_truncated_not_overflowed);

	printf("%d tests, %d failed\n", tests_run, tests_failed);
	return (tests_failed ? 1 : 0);
}
