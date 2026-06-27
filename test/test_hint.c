/*
 * Test harness for capture-hint module.
 *
 * Compile standalone: cc -o test_hint test_hint.c ../capture-hint.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../capture-hint.h"

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

#define ASSERT_INT_EQ(a, b, msg) \
	do { \
		if ((int)(a) != (int)(b)) { \
			printf("FAIL: %s (expected %d, got %d)\n", msg, \
			       (int)(b), (int)(a)); \
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

/* helper: check that all hints are unique */
static int
all_unique(struct capture_hint *hints, int count)
{
	int i, j;

	for (i = 0; i < count; i++) {
		for (j = i + 1; j < count; j++) {
			if (strcmp(hints[i].label, hints[j].label) == 0)
				return (0);
		}
	}
	return (1);
}

/* helper: check that no hint is a prefix of another */
static int
no_prefix(struct capture_hint *hints, int count)
{
	int    i, j;
	size_t len_i;

	for (i = 0; i < count; i++) {
		len_i = strlen(hints[i].label);
		for (j = 0; j < count; j++) {
			if (i == j)
				continue;
			if (strncmp(hints[i].label, hints[j].label, len_i) == 0)
				return (0);
		}
	}
	return (1);
}

/* helper: find the maximum label length */
static int
max_len(struct capture_hint *hints, int count)
{
	int i, m = 0;

	for (i = 0; i < count; i++) {
		if (hints[i].len > m)
			m = hints[i].len;
	}
	return (m);
}

TEST(zero_hints)
{
	struct capture_hint *hints;
	int		     count = -1;

	hints = capture_hint_generate(0, "abc", &count);
	ASSERT_INT_EQ(count, 0, "count should be 0");
	ASSERT(hints == NULL, "should return NULL for 0");

	capture_hint_free(hints, count);
}

TEST(single_char_hints)
{
	struct capture_hint *hints;
	int		     count = -1;

	hints = capture_hint_generate(3, "abc", &count);
	ASSERT_INT_EQ(count, 3, "should generate 3 hints");
	ASSERT(hints != NULL, "hints should not be NULL");
	ASSERT(all_unique(hints, count), "hints should be unique");
	ASSERT(no_prefix(hints, count), "hints should be prefix-free");
	ASSERT_INT_EQ(max_len(hints, count), 1, "all hints should be 1 char");

	capture_hint_free(hints, count);
}

TEST(mixed_length_optimal)
{
	struct capture_hint *hints;
	int		     count = -1;

	/*
	 * 3 keys, 7 hints. L=2 since 3 < 7 <= 9.
	 * n_short = floor((9 - 7) / 2) = 1, so 1 one-char + 6 two-char.
	 * The optimal distribution mixes lengths; max length must be 2.
	 */
	hints = capture_hint_generate(7, "abc", &count);
	ASSERT_INT_EQ(count, 7, "should generate 7 hints");
	ASSERT(all_unique(hints, count), "hints should be unique");
	ASSERT(no_prefix(hints, count), "hints should be prefix-free");
	ASSERT_INT_EQ(max_len(hints, count), 2, "max length should be 2");
	ASSERT_INT_EQ(hints[0].len, 1, "first hint should be 1 char (short)");

	capture_hint_free(hints, count);
}

TEST(default_keys)
{
	struct capture_hint *hints;
	int		     count = -1;

	/* Default keys: 9 chars. 30 hints -> L=2, mixed 1- and 2-char. */
	hints = capture_hint_generate(30, CAPTURE_DEFAULT_HINT_KEYS, &count);
	ASSERT_INT_EQ(count, 30, "should generate 30 hints");
	ASSERT(all_unique(hints, count), "hints should be unique");
	ASSERT(no_prefix(hints, count), "hints should be prefix-free");
	ASSERT_INT_EQ(max_len(hints, count), 2, "max length should be 2");

	capture_hint_free(hints, count);
}

TEST(three_char_hints)
{
	struct capture_hint *hints;
	int		     count = -1;

	/*
	 * 3 keys: 9 two-char combos max, so 10 needs three-char labels.
	 * The previous fixed-length algorithm failed here; the optimal
	 * algorithm must succeed with max length 3.
	 */
	hints = capture_hint_generate(10, "abc", &count);
	ASSERT_INT_EQ(count, 10, "should generate 10 hints");
	ASSERT(hints != NULL, "hints should not be NULL");
	ASSERT(all_unique(hints, count), "hints should be unique");
	ASSERT(no_prefix(hints, count), "hints should be prefix-free");
	ASSERT_INT_EQ(max_len(hints, count), 3, "max length should be 3");

	capture_hint_free(hints, count);
}

TEST(exact_power_boundary)
{
	struct capture_hint *hints;
	int		     count = -1;

	/* 3 keys, exactly 9 = 3^2 hints: all two-char, no shorter fits. */
	hints = capture_hint_generate(9, "abc", &count);
	ASSERT_INT_EQ(count, 9, "should generate 9 hints");
	ASSERT(all_unique(hints, count), "hints should be unique");
	ASSERT(no_prefix(hints, count), "hints should be prefix-free");
	ASSERT_INT_EQ(max_len(hints, count), 2, "all should be 2 char");

	capture_hint_free(hints, count);
}

TEST(single_key_rejected)
{
	struct capture_hint *hints;
	int		     count = -1;

	/* One key cannot form prefix-free multi-char hints. */
	hints = capture_hint_generate(5, "a", &count);
	ASSERT(hints == NULL, "should return NULL for single key");
	ASSERT_INT_EQ(count, 0, "count should be 0");

	capture_hint_free(hints, count);
}

TEST(empty_keys)
{
	struct capture_hint *hints;
	int		     count = -1;

	hints = capture_hint_generate(5, "", &count);
	ASSERT(hints == NULL, "should return NULL for empty keys");

	capture_hint_free(hints, count);
}

int
main(void)
{
	printf("capture-hint tests:\n");

	RUN_TEST(zero_hints);
	RUN_TEST(single_char_hints);
	RUN_TEST(mixed_length_optimal);
	RUN_TEST(default_keys);
	RUN_TEST(three_char_hints);
	RUN_TEST(exact_power_boundary);
	RUN_TEST(single_key_rejected);
	RUN_TEST(empty_keys);

	printf("\n%d tests, %d failed\n", tests_run, tests_failed);
	return (tests_failed > 0 ? 1 : 0);
}
