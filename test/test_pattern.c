/*
 * Test harness for capture-pattern module.
 *
 * Compile standalone: cc -o test_pattern test_pattern.c ../capture-pattern.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../capture-pattern.h"

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

TEST(compile_default_patterns)
{
	struct capture_pattern cp = { .valid = 0 };
	int		       rc;

	rc = capture_pattern_compile(&cp, CAPTURE_DEFAULT_PATTERNS);
	ASSERT(rc == 0, "compile should succeed");
	ASSERT(cp.valid == 1, "pattern should be valid");

	capture_pattern_free(&cp);
	ASSERT(cp.valid == 0, "free should mark invalid");
}

TEST(compile_empty_string)
{
	struct capture_pattern cp = { .valid = 0 };
	int		       rc;

	rc = capture_pattern_compile(&cp, "");
	ASSERT(rc == 0, "empty string should succeed");
	ASSERT(cp.valid == 0, "empty pattern should not be valid");

	capture_pattern_free(&cp);
}

TEST(compile_simple_pattern)
{
	struct capture_pattern cp = { .valid = 0 };
	int		       rc;

	rc = capture_pattern_compile(&cp, "[0-9]+");
	ASSERT(rc == 0, "compile should succeed");
	ASSERT(cp.valid == 1, "pattern should be valid");

	capture_pattern_free(&cp);
}

TEST(compile_invalid_regex)
{
	struct capture_pattern cp = { .valid = 0 };
	int		       rc;

	rc = capture_pattern_compile(&cp, "[unclosed");
	ASSERT(rc != 0, "invalid regex should fail");
	ASSERT(cp.valid == 0, "invalid pattern should not be valid");

	capture_pattern_free(&cp);
}

TEST(match_url)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	capture_pattern_compile(&cp, "https?://[^[:space:]]+");

	matches = capture_pattern_match_line(
		&cp, "visit https://example.com/path today", 0, &count);
	ASSERT_INT_EQ(count, 1, "should find 1 URL");
	ASSERT(matches != NULL, "matches should not be NULL");
	ASSERT_STR_EQ(matches[0].text, "https://example.com/path",
		      "should match URL");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_email)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	capture_pattern_compile(
		&cp, "[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}");

	matches = capture_pattern_match_line(
		&cp, "Contact user@example.com for help", 0, &count);
	ASSERT_INT_EQ(count, 1, "should find 1 email");
	ASSERT_STR_EQ(matches[0].text, "user@example.com",
		      "should match email");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_home_path)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	/* The default patterns must recognise ~ and relative paths. */
	capture_pattern_compile(&cp, CAPTURE_DEFAULT_PATTERNS);

	matches = capture_pattern_match_line(
		&cp, "edit ~/.config/tmux/tmux.conf now", 0, &count);
	ASSERT(count >= 1, "should find the home path");
	ASSERT_STR_EQ(matches[0].text, "~/.config/tmux/tmux.conf",
		      "should include the ~ prefix");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_absolute_and_relative_path)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	capture_pattern_compile(&cp, CAPTURE_DEFAULT_PATTERNS);

	matches = capture_pattern_match_line(
		&cp, "see /etc/hosts and ./src/main.c", 0, &count);
	ASSERT_INT_EQ(count, 2, "should find both paths");
	ASSERT_STR_EQ(matches[0].text, "/etc/hosts",
		      "absolute path");
	ASSERT_STR_EQ(matches[1].text, "./src/main.c",
		      "relative path with ./ prefix");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_dotfile_relative_path)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	capture_pattern_compile(&cp, CAPTURE_DEFAULT_PATTERNS);

	/*
	 * A relative path whose first segment is a plain or dotfile name
	 * (not ~, . or ..) must be matched in full, not truncated to the
	 * first slash. Regression: ".plans/..." used to match only from
	 * "/FEAT-256...".
	 */
	matches = capture_pattern_match_line(
		&cp,
		"see .plans/FEAT-256-tui-app-extraction-260713/"
		"structure-refactor-report.md now",
		0, &count);
	ASSERT(count >= 1, "should find the dotfile relative path");
	ASSERT_STR_EQ(matches[0].text,
		      ".plans/FEAT-256-tui-app-extraction-260713/"
		      "structure-refactor-report.md",
		      "should include the leading .plans segment");
	capture_match_free(matches, count);
	matches = NULL;
	count = 0;

	/* A plain relative first segment (no leading dot) must also survive. */
	matches = capture_pattern_match_line(
		&cp, "open src/main.c please", 0, &count);
	ASSERT(count >= 1, "should find the plain relative path");
	ASSERT_STR_EQ(matches[0].text, "src/main.c",
		      "should include the leading src segment");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(default_matches_git_sha)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	char		      *pattern;
	int		       count = 0;

	/* By default the git-sha category is active. */
	pattern = capture_pattern_default(NULL);
	capture_pattern_compile(&cp, pattern);
	free(pattern);

	matches = capture_pattern_match_line(&cp, "commit a543312d done", 0,
					     &count);
	ASSERT(count >= 1, "git sha matched by default");
	ASSERT_STR_EQ(matches[0].text, "a543312d", "git sha text");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(disable_single_category)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	char		      *pattern;
	int		       count = 0;

	/* Disabling git-sha removes it but keeps every other category. */
	pattern = capture_pattern_default("git-sha");
	capture_pattern_compile(&cp, pattern);
	free(pattern);

	matches = capture_pattern_match_line(&cp, "commit a543312d done", 0,
					     &count);
	ASSERT_INT_EQ(count, 0, "git sha not matched when disabled");
	capture_match_free(matches, count);
	matches = NULL;
	count = 0;

	/* URLs still work after disabling an unrelated category. */
	matches = capture_pattern_match_line(
		&cp, "see https://example.com/x here", 0, &count);
	ASSERT_INT_EQ(count, 1, "url still matched");
	ASSERT_STR_EQ(matches[0].text, "https://example.com/x", "url text");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(disable_multiple_categories)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	char		      *pattern;
	int		       count = 0;

	/* Whitespace around names in the comma list is tolerated. */
	pattern = capture_pattern_default("git-sha, ipv4");
	capture_pattern_compile(&cp, pattern);
	free(pattern);

	matches = capture_pattern_match_line(
		&cp, "host 10.0.0.1 sha a543312d done", 0, &count);
	ASSERT_INT_EQ(count, 0, "both disabled categories gone");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(disable_unknown_name_ignored)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	char		      *pattern;
	int		       count = 0;

	/* An unknown category name disables nothing. */
	pattern = capture_pattern_default("bogus-name");
	capture_pattern_compile(&cp, pattern);
	free(pattern);

	matches = capture_pattern_match_line(&cp, "commit a543312d done", 0,
					     &count);
	ASSERT(count >= 1, "unknown disable name leaves defaults intact");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_multiple)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	capture_pattern_compile(&cp, "[0-9]+");

	matches = capture_pattern_match_line(&cp, "hello 123 world 456 end", 0,
					     &count);
	ASSERT_INT_EQ(count, 2, "should find 2 numbers");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_none)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	capture_pattern_compile(&cp, "[0-9]+");

	matches = capture_pattern_match_line(&cp, "no numbers here", 0, &count);
	ASSERT_INT_EQ(count, 0, "should find no matches");
	ASSERT(matches == NULL, "matches should be NULL");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_with_alternation)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	/* Combined pattern: email OR digits */
	capture_pattern_compile(&cp, "[a-z]+@[a-z]+\\.[a-z]+|[0-9]+");

	matches = capture_pattern_match_line(
		&cp, "hello user@dom.com 12345 world", 0, &count);
	ASSERT(count >= 2, "should find email and number");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_hex_color_6)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	capture_pattern_compile(&cp, CAPTURE_DEFAULT_PATTERNS);

	matches = capture_pattern_match_line(
		&cp, "color #ff0000 and #aBc123 end", 0, &count);
	ASSERT_INT_EQ(count, 2, "should find 2 hex colors");
	ASSERT_STR_EQ(matches[0].text, "#ff0000", "first hex");
	ASSERT_STR_EQ(matches[1].text, "#aBc123", "second hex mixed case");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_hex_color_3)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	capture_pattern_compile(&cp, CAPTURE_DEFAULT_PATTERNS);

	matches = capture_pattern_match_line(
		&cp, "shorthand #fff and #1a2", 0, &count);
	ASSERT_INT_EQ(count, 2, "should find 2 short hex colors");
	ASSERT_STR_EQ(matches[0].text, "#fff", "3-char hex");
	ASSERT_STR_EQ(matches[1].text, "#1a2", "3-char hex mixed case");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_hex_color_8)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	capture_pattern_compile(&cp, CAPTURE_DEFAULT_PATTERNS);

	matches = capture_pattern_match_line(
		&cp, "rgba #ff0000ff end", 0, &count);
	ASSERT_INT_EQ(count, 1, "should find 8-char hex with alpha");
	ASSERT_STR_EQ(matches[0].text, "#ff0000ff", "8-char hex");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_hex_color_boundary)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	capture_pattern_compile(&cp, CAPTURE_DEFAULT_PATTERNS);

	/*
	 * 6-char hex must NOT match when followed by more hex chars
	 * (it would be a false positive inside an 8-char hex like #ff0000ff).
	 * The 8-char pattern should claim it instead.
	 */
	matches = capture_pattern_match_line(
		&cp, "color #ff0000ff here", 0, &count);
	ASSERT_INT_EQ(count, 1, "8-char hex should claim, not 6-char");
	ASSERT_STR_EQ(matches[0].text, "#ff0000ff", "8-char hex wins");
	capture_match_free(matches, count);
	matches = NULL;
	count = 0;

	/*
	 * 3-char hex must NOT match when followed by more hex chars
	 * (it would be a false positive inside a longer hex).
	 */
	matches = capture_pattern_match_line(
		&cp, "test #fff000", 0, &count);
	ASSERT_INT_EQ(count, 1, "6-char hex should claim, not 3-char");
	ASSERT_STR_EQ(matches[0].text, "#fff000", "6-char hex wins");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_url_trailing_paren)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	capture_pattern_compile(&cp, "https?://[^[:space:]]+");

	/* A trailing ) from surrounding prose must not be part of the URL. */
	matches = capture_pattern_match_line(
		&cp, "see (https://youtube.com) now", 0, &count);
	ASSERT_INT_EQ(count, 1, "should find 1 URL");
	ASSERT_STR_EQ(matches[0].text, "https://youtube.com",
		      "trailing ) should be stripped");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_url_trailing_punct)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	capture_pattern_compile(&cp, "https?://[^[:space:]]+");

	/* Sentence-ending punctuation must be trimmed. */
	matches = capture_pattern_match_line(
		&cp, "visit https://example.com/path.", 0, &count);
	ASSERT_INT_EQ(count, 1, "should find 1 URL");
	ASSERT_STR_EQ(matches[0].text, "https://example.com/path",
		      "trailing . should be stripped");
	capture_match_free(matches, count);
	matches = NULL;
	count = 0;

	matches = capture_pattern_match_line(
		&cp, "go to https://example.com, please", 0, &count);
	ASSERT_INT_EQ(count, 1, "should find 1 URL");
	ASSERT_STR_EQ(matches[0].text, "https://example.com",
		      "trailing , should be stripped");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(match_url_balanced_paren)
{
	struct capture_pattern cp = { .valid = 0 };
	struct capture_match  *matches;
	int		       count = 0;

	capture_pattern_compile(&cp, "https?://[^[:space:]]+");

	/* Balanced parens inside a URL (e.g. Wikipedia) must be preserved. */
	matches = capture_pattern_match_line(
		&cp, "see https://en.wikipedia.org/wiki/Foo_(disambiguation)",
		0, &count);
	ASSERT_INT_EQ(count, 1, "should find 1 URL");
	ASSERT_STR_EQ(matches[0].text,
		      "https://en.wikipedia.org/wiki/Foo_(disambiguation)",
		      "balanced parens should be kept");
	capture_match_free(matches, count);
	matches = NULL;
	count = 0;

	/* Balanced parens kept, but an extra unbalanced ) is trimmed. */
	matches = capture_pattern_match_line(
		&cp, "(https://en.wikipedia.org/wiki/Foo_(bar))", 0, &count);
	ASSERT_INT_EQ(count, 1, "should find 1 URL");
	ASSERT_STR_EQ(matches[0].text,
		      "https://en.wikipedia.org/wiki/Foo_(bar)",
		      "only the outer unbalanced ) should be stripped");

	capture_match_free(matches, count);
	capture_pattern_free(&cp);
}

TEST(overlap_longer_wins)
{
	struct capture_match *matches;
	int		      count = 2;

	matches = calloc(2, sizeof *matches);

	matches[0].sx = 5;
	matches[0].sy = 0;
	matches[0].ex = 8;
	matches[0].ey = 0;
	matches[0].text = strdup("123");

	matches[1].sx = 0;
	matches[1].sy = 0;
	matches[1].ex = 8;
	matches[1].ey = 0;
	matches[1].text = strdup("hello123");

	capture_pattern_resolve_overlaps(matches, &count);
	ASSERT_INT_EQ(count, 1, "should resolve to 1 match");
	ASSERT_STR_EQ(matches[0].text, "hello123", "longer match should win");

	capture_match_free(matches, count);
}

TEST(overlap_non_overlapping)
{
	struct capture_match *matches;
	int		      count = 2;

	matches = calloc(2, sizeof *matches);

	matches[0].sx = 0;
	matches[0].sy = 0;
	matches[0].ex = 3;
	matches[0].ey = 0;
	matches[0].text = strdup("abc");

	matches[1].sx = 5;
	matches[1].sy = 0;
	matches[1].ex = 8;
	matches[1].ey = 0;
	matches[1].text = strdup("def");

	capture_pattern_resolve_overlaps(matches, &count);
	ASSERT_INT_EQ(count, 2, "non-overlapping should both be kept");

	capture_match_free(matches, count);
}

TEST(overlap_same_length_earlier_wins)
{
	struct capture_match *matches;
	int		      count = 2;

	matches = calloc(2, sizeof *matches);

	matches[0].sx = 0;
	matches[0].sy = 0;
	matches[0].ex = 3;
	matches[0].ey = 0;
	matches[0].text = strdup("abc");

	matches[1].sx = 0;
	matches[1].sy = 0;
	matches[1].ex = 3;
	matches[1].ey = 0;
	matches[1].text = strdup("abc");

	capture_pattern_resolve_overlaps(matches, &count);
	ASSERT_INT_EQ(count, 1, "should resolve to 1 match");
	ASSERT_STR_EQ(matches[0].text, "abc", "earlier match should win");

	capture_match_free(matches, count);
}

int
main(void)
{
	printf("capture-pattern tests:\n");

	RUN_TEST(compile_default_patterns);
	RUN_TEST(compile_empty_string);
	RUN_TEST(compile_simple_pattern);
	RUN_TEST(compile_invalid_regex);
	RUN_TEST(match_url);
	RUN_TEST(match_email);
	RUN_TEST(match_home_path);
	RUN_TEST(match_absolute_and_relative_path);
	RUN_TEST(match_dotfile_relative_path);
	RUN_TEST(default_matches_git_sha);
	RUN_TEST(disable_single_category);
	RUN_TEST(disable_multiple_categories);
	RUN_TEST(disable_unknown_name_ignored);
	RUN_TEST(match_multiple);
	RUN_TEST(match_none);
	RUN_TEST(match_with_alternation);
	RUN_TEST(match_hex_color_6);
	RUN_TEST(match_hex_color_3);
	RUN_TEST(match_hex_color_8);
	RUN_TEST(match_hex_color_boundary);
	RUN_TEST(match_url_trailing_paren);
	RUN_TEST(match_url_trailing_punct);
	RUN_TEST(match_url_balanced_paren);
	RUN_TEST(overlap_longer_wins);
	RUN_TEST(overlap_non_overlapping);
	RUN_TEST(overlap_same_length_earlier_wins);

	printf("\n%d tests, %d failed\n", tests_run, tests_failed);
	return (tests_failed > 0 ? 1 : 0);
}
