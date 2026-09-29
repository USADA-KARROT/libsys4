/* Pins the current (SJIS) behaviour of the string character helpers.
 *
 * Covers sjis_index/sjis_count_char, string_copy/find/push_back/pop_back/
 * erase/get_char/set_char, int_to_cstr/float_to_cstr with zenkaku digits and
 * string_to_integer, for SJIS text and for non-SJIS high bytes: character
 * codes are little-endian (lead byte in the low byte), single bytes come back
 * as a plain char, push_back(0) appends a NUL byte and an out-of-range index
 * calls ERROR (caught here through sys_error_handler).
 *
 * Only the default character rule is exercised; this file must keep passing
 * unchanged when other rules are added.
 */
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

static jmp_buf error_jmp;
static volatile int nr_errors;

static void on_error(const char *msg)
{
	(void)msg;
	nr_errors++;
	longjmp(error_jmp, 1);
}

// Runs stmt and asserts that it called ERROR exactly once.
#define EXPECT_ERROR(stmt) do { \
		int before_ = nr_errors; \
		if (!setjmp(error_jmp)) { \
			stmt; \
			assert(!"expected ERROR"); \
		} \
		assert(nr_errors == before_ + 1); \
	} while (0)

static struct string *str(const char *bytes)
{
	return make_string(bytes, strlen(bytes));
}

static bool str_is(const struct string *s, const char *bytes, int len)
{
	return s->size == len && !memcmp(s->text, bytes, len) && s->text[len] == '\0';
}

static void test_count_and_index(void)
{
	assert(sjis_count_char("") == 0);
	assert(sjis_count_char("abc") == 3);
	assert(sjis_count_char("\x82\xA0\x82\xA2") == 2);
	// GBK bytes: 0xBE and 0xC7 are single bytes, 0x89 pairs with what follows
	assert(sjis_count_char("\xBE\x5F\xC7\x89\xBE\x5F\xC7\x89") == 7);
	// truncated 2-byte character at the end counts as one
	assert(sjis_count_char("\x82") == 1);

	assert(sjis_index("\x82\xA0" "a", 1) == 2);
	assert(sjis_index("\x82\xA0" "a", 2) == -1);
	assert(sjis_index("\x82\xA0" "a", -1) == 0);
	assert(sjis_index("", 0) == -1);
}

static void test_find_and_copy(void)
{
	// GBK bytes of a 5-character string; the SJIS walk sees 8 characters
	struct string *hay = str("\xC1\xA2\xC0\x4C\xA3\xAF\xB0\xA2\xD0\xDC");
	struct string *needle = str("\xB0\xA2\xD0\xDC");
	struct string *empty = str("");
	assert(string_find(hay, needle) == 6);
	assert(string_find(hay, empty) == 0);
	assert(string_find(empty, empty) == -1);
	free_string(hay);
	free_string(needle);

	struct string *s = str("\x82\xA0\x82\xA2");
	struct string *c = string_copy(s, 1, 1);
	assert(str_is(c, "\x82\xA2", 2));
	free_string(c);
	c = string_copy(s, -3, 1);
	assert(str_is(c, "\x82\xA0", 2));
	free_string(c);
	c = string_copy(s, 2, 1);
	assert(str_is(c, "", 0));
	free_string(c);
	c = string_copy(s, 0, 0);
	assert(str_is(c, "", 0));
	free_string(c);
	free_string(s);
	free_string(empty);
}

static void test_push_pop_erase(void)
{
	struct string *s = str("");
	string_push_back(&s, 0xA082);
	assert(str_is(s, "\x82\xA0", 2));
	string_push_back(&s, 'a');
	assert(str_is(s, "\x82\xA0" "a", 3));
	free_string(s);

	s = str("x");
	string_push_back(&s, 0);
	assert(s->size == 2 && s->text[0] == 'x' && s->text[1] == '\0');
	free_string(s);

	// the high byte is dropped unless the low byte is an SJIS lead
	s = str("");
	string_push_back(&s, 0xA3B0);
	assert(str_is(s, "\xB0", 1));
	free_string(s);

	s = str("\xBE\x5F\xC7\x89");
	string_pop_back(&s);
	assert(str_is(s, "\xBE\x5F\xC7", 3));
	free_string(s);

	s = str("\x82\xA0\x82\xA2");
	string_pop_back(&s);
	assert(str_is(s, "\x82\xA0", 2));
	free_string(s);

	s = str("\x82\xA0" "a");
	string_erase(&s, 0);
	assert(str_is(s, "a", 1));
	string_erase(&s, 5);
	assert(str_is(s, "a", 1));
	free_string(s);
}

static void test_get_set_char(void)
{
	struct string *s = str("\x82\xA0");
	assert(string_get_char(s, 0) == 0xA082);
	free_string(s);

	s = str("\xB1");
	assert(string_get_char(s, 0) == (int)(char)0xB1);
	free_string(s);

	s = str("ab");
	assert(string_get_char(s, 1) == 'b');
	assert(string_get_char(s, 2) == 0);
	EXPECT_ERROR(string_get_char(s, 99));
	EXPECT_ERROR(string_get_char(s, -1));
	free_string(s);

	s = str("ab");
	string_set_char(&s, 0, 0xA082);
	assert(str_is(s, "\x82\xA0" "b", 3));
	string_set_char(&s, 0, 'z');
	assert(str_is(s, "zb", 2));
	string_set_char(&s, 1, 0);
	assert(str_is(s, "z", 1));
	free_string(s);

	s = str("ab");
	EXPECT_ERROR(string_set_char(&s, 5, 'x'));
	free_string(s);
}

static void test_numbers(void)
{
	char buf[512];
	int len = int_to_cstr(buf, sizeof buf, 12, 0, false, true);
	assert(len == 4 && !memcmp(buf, "\x82\x50\x82\x51", 5));
	len = int_to_cstr(buf, sizeof buf, -1, 0, false, true);
	assert(len == 4 && !memcmp(buf, "\x81\x7C\x82\x50", 5));
	len = int_to_cstr(buf, sizeof buf, 5, 2, true, true);
	assert(len == 4 && !memcmp(buf, "\x82\x4F\x82\x54", 5));
	len = int_to_cstr(buf, sizeof buf, 12, 0, false, false);
	assert(len == 2 && !strcmp(buf, "12"));
	len = float_to_cstr(buf, sizeof buf, 1.5f, 0, false, 1, true);
	assert(len == 7 && !memcmp(buf, "\x82\x50\x81\x44\x82\x54" "F", 8));

	struct string *s = str("\x82\x50\x82\x51");
	assert(string_to_integer(s) == 12);
	free_string(s);
	s = str("\x81\x7C\x82\x53");
	assert(string_to_integer(s) == -4);
	free_string(s);
	s = str("34");
	assert(string_to_integer(s) == 34);
	free_string(s);
}

int main(void)
{
	sys_error_handler = on_error;
	test_count_and_index();
	test_find_and_copy();
	test_push_pop_erase();
	test_get_set_char();
	test_numbers();
	printf("string_sjis: all tests passed\n");
	return 0;
}
