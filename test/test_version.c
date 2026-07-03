/*
 * Test harness for version-format module.
 *
 * Compile standalone: cc -o test_version test_version.c ../version-format.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../version-format.h"

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

#define ASSERT_STR_EQ(a, b, msg) \
	do { \
		if (strcmp((a), (b)) != 0) { \
			printf("FAIL: %s (expected '%s', got '%s')\n", msg, b, \
			       a); \
			tests_failed++; \
			return; \
		} \
	} while (0)

/* Fixed "now" for deterministic relative-time buckets (UTC). */
#define NOW ((time_t)1751537602L)	/* 2025-07-03T10:13:22Z */

/* --- version_format_short --------------------------------------------- */

TEST(short_with_hash)
{
	char	 buf[64];

	version_format_short(buf, sizeof buf, "next-3.7", "94c8b83");
	ASSERT_STR_EQ(buf, "next-3.7+g94c8b83", "short with hash");
}

TEST(short_no_hash)
{
	char	 buf[64];

	version_format_short(buf, sizeof buf, "next-3.7", "");
	ASSERT_STR_EQ(buf, "next-3.7", "short without hash");
}

/* --- version_format_relative ------------------------------------------ */

TEST(relative_just_now)
{
	char	 buf[64];

	version_format_relative(buf, sizeof buf, NOW - 30, NOW);
	ASSERT_STR_EQ(buf, "just now", "under 60s");
}

TEST(relative_minutes_plural)
{
	char	 buf[64];

	version_format_relative(buf, sizeof buf, NOW - 5 * 60, NOW);
	ASSERT_STR_EQ(buf, "5 min ago", "5 minutes");
}

TEST(relative_minutes_singular)
{
	char	 buf[64];

	version_format_relative(buf, sizeof buf, NOW - 90, NOW);
	ASSERT_STR_EQ(buf, "1 min ago", "90s -> 1 min");
}

TEST(relative_hours_plural)
{
	char	 buf[64];

	version_format_relative(buf, sizeof buf, NOW - 3 * 3600, NOW);
	ASSERT_STR_EQ(buf, "3 hours ago", "3 hours");
}

TEST(relative_hours_singular)
{
	char	 buf[64];

	version_format_relative(buf, sizeof buf, NOW - 75 * 60, NOW);
	ASSERT_STR_EQ(buf, "1 hour ago", "75 min -> 1 hour");
}

TEST(relative_yesterday)
{
	char	 buf[64];

	version_format_relative(buf, sizeof buf, NOW - 25 * 3600, NOW);
	ASSERT_STR_EQ(buf, "yesterday", "25 hours -> yesterday");
}

TEST(relative_days)
{
	char	 buf[64];

	version_format_relative(buf, sizeof buf, NOW - 3 * 86400, NOW);
	ASSERT_STR_EQ(buf, "3 days ago", "3 days");
}

TEST(relative_far_past_iso_date)
{
	char	 buf[64];

	/* 30 days before 2025-07-03 is 2025-06-03 (UTC). */
	version_format_relative(buf, sizeof buf, NOW - 30 * 86400, NOW);
	ASSERT_STR_EQ(buf, "2025-06-03", "far past -> iso date");
}

TEST(relative_future_skew)
{
	char	 buf[64];

	version_format_relative(buf, sizeof buf, NOW + 30, NOW);
	ASSERT_STR_EQ(buf, "just now", "future clock skew -> just now");
}

/* --- version_format_full ---------------------------------------------- */

TEST(full_with_hash)
{
	char	 buf[128];

	/* build far in the past so the relative bucket is a stable ISO date. */
	version_format_full(buf, sizeof buf, "next-3.7", "94c8b83",
	    NOW - 30 * 86400, NOW);
	ASSERT_STR_EQ(buf,
	    "next-3.7+g94c8b83 (released 2025-06-03T10:13:22Z, 2025-06-03)",
	    "full rich line");
}

TEST(full_hours_ago)
{
	char	 buf[128];

	version_format_full(buf, sizeof buf, "next-3.7", "94c8b83",
	    NOW - 6 * 3600, NOW);
	ASSERT_STR_EQ(buf,
	    "next-3.7+g94c8b83 (released 2025-07-03T04:13:22Z, 6 hours ago)",
	    "full rich line, hours ago");
}

TEST(full_no_hash)
{
	char	 buf[128];

	version_format_full(buf, sizeof buf, "next-3.7", "", NOW, NOW);
	ASSERT_STR_EQ(buf, "next-3.7", "no hash -> bare base, no clause");
}

TEST(full_zero_epoch)
{
	char	 buf[128];

	version_format_full(buf, sizeof buf, "next-3.7", "94c8b83", 0, NOW);
	ASSERT_STR_EQ(buf, "next-3.7+g94c8b83", "zero epoch -> short only");
}

int
main(void)
{
	printf("version-format tests:\n");

	RUN_TEST(short_with_hash);
	RUN_TEST(short_no_hash);
	RUN_TEST(relative_just_now);
	RUN_TEST(relative_minutes_plural);
	RUN_TEST(relative_minutes_singular);
	RUN_TEST(relative_hours_plural);
	RUN_TEST(relative_hours_singular);
	RUN_TEST(relative_yesterday);
	RUN_TEST(relative_days);
	RUN_TEST(relative_far_past_iso_date);
	RUN_TEST(relative_future_skew);
	RUN_TEST(full_with_hash);
	RUN_TEST(full_hours_ago);
	RUN_TEST(full_no_hash);
	RUN_TEST(full_zero_epoch);

	printf("\n%d tests, %d failed\n", tests_run, tests_failed);
	return (tests_failed > 0 ? 1 : 0);
}
