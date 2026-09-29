/* Tests for the GBK character rule (sys4_set_string_charset).
 *
 * Checks mbcs_index/mbcs_count_char, the GBK branches of string_copy/find/
 * push_back/pop_back/erase/get_char/set_char, zenkaku numbers and
 * string_to_integer, that out-of-range accesses never call ERROR, that
 * string_push_back_sjis/string_pop_back_sjis keep the SJIS packing, that the
 * rule can be switched back, and (differentially, on random bytes) that the
 * mbcs_* functions equal sjis_* under the default rule.
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

// GBK sample bytes (no game text beyond a few characters)
#define QI_LA   "\xBE\x5F\xC7\x89"                      // 2 characters
#define PANEL   "\xC1\xA2\xC0\x4C\xA3\xAF\xB0\xA2\xD0\xDC" // 5 characters, 3rd is A3 AF
#define TAIL_A  "\xB4\x61"                              // trail byte 'a'
#define TAIL_BS "\xD5\x5C"                              // trail byte '\\'
#define TAIL_VB "\xDF\x7C"                              // trail byte '|'

static void test_count_and_index(void)
{
	assert(mbcs_count_char("") == 0);
	assert(mbcs_count_char(QI_LA QI_LA) == 4);
	assert(mbcs_count_char("\xBE") == 1);
	assert(mbcs_count_char("a\xBE") == 2);
	assert(mbcs_count_char(PANEL) == 5);
	// 0x80 and 0xFF are single bytes
	assert(mbcs_count_char("\x80\xFF") == 2);

	assert(mbcs_index(QI_LA, 1) == 2);
	assert(mbcs_index(QI_LA, 2) == -1);
	assert(mbcs_index(QI_LA, -1) == 0);
	assert(mbcs_index("", 0) == -1);
	assert(mbcs_index("a\xBE", 1) == 1);
	assert(mbcs_index("a\xBE", 2) == -1);
}

static void test_find_and_copy(void)
{
	struct string *hay = str(PANEL);
	struct string *needle = str("\xB0\xA2\xD0\xDC");
	struct string *empty = str("");
	assert(string_find(hay, needle) == 3);
	assert(string_find(hay, empty) == 0);
	assert(string_find(empty, empty) == -1);
	free_string(needle);

	// the trail byte of a character is never a match start
	struct string *s = str(TAIL_VB "x");
	needle = str("|");
	assert(string_find(s, needle) == -1);
	free_string(s);
	free_string(needle);

	// embedded NUL bytes inside the size still terminate
	s = make_string("a\0\0b", 4);
	needle = str("b");
	assert(string_find(s, needle) == 3);
	free_string(s);
	s = make_string("\xBE\0b", 3);
	assert(string_find(s, needle) == 1);
	free_string(s);
	free_string(needle);

	struct string *c = string_copy(hay, 3, 2);
	assert(str_is(c, "\xB0\xA2\xD0\xDC", 4));
	free_string(c);
	c = string_copy(hay, 2, 1);
	assert(str_is(c, "\xA3\xAF", 2));
	free_string(c);
	c = string_copy(hay, 5, 1);
	assert(str_is(c, "", 0));
	free_string(c);
	free_string(hay);
	free_string(empty);
}

static void test_push_pop_erase(void)
{
	struct string *s = str("");
	string_push_back(&s, 0xA3B0);
	assert(str_is(s, "\xA3\xB0", 2));
	string_push_back(&s, 92);
	assert(str_is(s, "\xA3\xB0\\", 3));
	string_push_back(&s, 0);
	assert(str_is(s, "\xA3\xB0\\", 3));
	string_push_back(&s, 0x8100);
	assert(str_is(s, "\xA3\xB0\\\x81", 4));
	free_string(s);

	s = str("");
	string_push_back(&s, -1);
	assert(str_is(s, "\xFF", 1));
	string_push_back(&s, 0x1A3B0);
	assert(str_is(s, "\xFF\xA3\xB0", 3));
	string_push_back(&s, 0x10000);
	assert(str_is(s, "\xFF\xA3\xB0", 3));
	free_string(s);

	s = str(QI_LA);
	string_pop_back(&s);
	assert(str_is(s, "\xBE\x5F", 2));
	string_pop_back(&s);
	assert(str_is(s, "", 0));
	string_pop_back(&s);
	assert(str_is(s, "", 0));
	free_string(s);

	s = str("a\xBE");
	string_pop_back(&s);
	assert(str_is(s, "a", 1));
	free_string(s);

	// copy-on-write: a shared string is not modified in place
	struct string *shared = str(QI_LA);
	struct string *ref = string_ref(shared);
	string_pop_back(&ref);
	assert(str_is(ref, "\xBE\x5F", 2) && str_is(shared, QI_LA, 4));
	free_string(ref);
	free_string(shared);

	s = str(QI_LA);
	string_erase(&s, 0);
	assert(str_is(s, "\xC7\x89", 2));
	string_erase(&s, 1);
	assert(str_is(s, "\xC7\x89", 2));
	string_erase(&s, -1);
	assert(str_is(s, "", 0));
	free_string(s);

	s = str("a\xBE");
	string_erase(&s, 1);
	assert(str_is(s, "a", 1));
	free_string(s);
}

static void test_get_char(void)
{
	int errors = nr_errors;
	struct string *s = str(QI_LA);
	assert(string_get_char(s, 0) == 0xBE5F);
	assert(string_get_char(s, 1) == 0xC789);
	assert(string_get_char(s, 2) == 0);   // == length: silent
	assert(string_get_char(s, 99) == 0);  // past the end: warning, no ERROR
	assert(string_get_char(s, -5) == 0xBE5F);
	free_string(s);

	s = str("a\xB1");
	assert(string_get_char(s, 0) == 'a');
	assert(string_get_char(s, 1) == 0xB100); // lone lead byte: (lead << 8)
	free_string(s);

	s = str("\xB1");
	assert(string_get_char(s, 0) == 0xB100);
	free_string(s);

	s = str("\x80\xFF");
	assert(string_get_char(s, 0) == 0x80);   // unsigned single bytes
	assert(string_get_char(s, 1) == 0xFF);
	free_string(s);

	s = str("");
	assert(string_get_char(s, 0) == 0);
	assert(string_get_char(s, -1) == 0);
	assert(string_get_char(s, 3) == 0);
	free_string(s);
	assert(nr_errors == errors);
}

static void test_set_char(void)
{
	int errors = nr_errors;
	struct string *s = str(QI_LA);
	string_set_char(&s, 1, 0xA3B0);           // 2 -> 2
	assert(str_is(s, "\xBE\x5F\xA3\xB0", 4));
	string_set_char(&s, 0, 'x');              // 2 -> 1, shrinks
	assert(str_is(s, "x\xA3\xB0", 3));
	string_set_char(&s, 0, 0xC789);           // 1 -> 2, grows
	assert(str_is(s, "\xC7\x89\xA3\xB0", 4));
	string_set_char(&s, 1, 'y');
	assert(str_is(s, "\xC7\x89y", 3));
	string_set_char(&s, 2, 'z');              // == length: ignored
	string_set_char(&s, 9, 'z');              // past the end: ignored
	string_set_char(&s, -1, 'z');             // negative: ignored
	assert(str_is(s, "\xC7\x89y", 3));
	string_set_char(&s, 1, 0x1A3B0);          // (uint16_t)c decides
	assert(str_is(s, "\xC7\x89\xA3\xB0", 4));
	string_set_char(&s, 1, 0x10041);
	assert(str_is(s, "\xC7\x89" "A", 3));
	string_set_char(&s, 0, 0x4100);           // ends in a NUL byte: truncates
	assert(str_is(s, "A", 1));
	string_set_char(&s, 0, 0);                // truncates
	assert(str_is(s, "", 0));
	free_string(s);

	s = str("a\xBE");
	string_set_char(&s, 1, 0xA3B0);           // lone lead byte counts as 1 byte
	assert(str_is(s, "a\xA3\xB0", 3));
	free_string(s);

	// copy-on-write
	struct string *shared = str(QI_LA);
	struct string *ref = string_ref(shared);
	string_set_char(&ref, 0, 'q');
	assert(str_is(ref, "q\xC7\x89", 3) && str_is(shared, QI_LA, 4));
	free_string(ref);
	free_string(shared);
	assert(nr_errors == errors);
}

static void test_numbers(void)
{
	char buf[512];
	int len = int_to_cstr(buf, sizeof buf, 12, 0, false, true);
	assert(len == 4 && !memcmp(buf, "\xA3\xB1\xA3\xB2", 5));
	len = int_to_cstr(buf, sizeof buf, -1, 0, false, true);
	assert(len == 4 && !memcmp(buf, "\x81\x7C\xA3\xB1", 5));
	len = int_to_cstr(buf, sizeof buf, 5, 2, true, true);
	assert(len == 4 && !memcmp(buf, "\xA3\xB0\xA3\xB5", 5));
	len = int_to_cstr(buf, sizeof buf, 7, 3, false, true);
	assert(len == 6 && !memcmp(buf, "\x81\x40\x81\x40\xA3\xB7", 7));
	len = int_to_cstr(buf, sizeof buf, 12, 0, false, false);
	assert(len == 2 && !strcmp(buf, "12"));
	// no trailing 'F' under the GBK rule
	len = float_to_cstr(buf, sizeof buf, 1.5f, 0, false, 1, true);
	assert(len == 6 && !memcmp(buf, "\xA3\xB1\x81\x44\xA3\xB5", 7));

	struct string *s = str("\xA3\xB1\xA3\xB2");
	assert(string_to_integer(s) == 12);
	free_string(s);
	s = str("-\xA3\xB3");
	assert(string_to_integer(s) == -3);
	free_string(s);
	// SJIS full-width digits are not converted under the GBK rule
	s = str("\x82\x50");
	assert(string_to_integer(s) == 0);
	free_string(s);
	// a lead byte before the NUL
	s = str("1\xA3");
	assert(string_to_integer(s) == 1);
	free_string(s);

	strcpy(buf, "\xA3\xB1\x81\x44\xA3\xB5x\xA3\xAE");
	string_zen2han_number(buf);
	assert(!strcmp(buf, "1.5x\xA3\xAE"));
	strcpy(buf, "\xA3");
	string_zen2han_number(buf);
	assert(!strcmp(buf, "\xA3"));
}

static void test_sjis_helpers(void)
{
	// the explicit SJIS helpers ignore the active rule
	struct string *s = str("");
	string_push_back_sjis(&s, 0xA082);
	assert(str_is(s, "\x82\xA0", 2));
	string_push_back_sjis(&s, 0xA3B0);
	assert(str_is(s, "\x82\xA0\xB0", 3));
	string_push_back_sjis(&s, -119);
	assert(str_is(s, "\x82\xA0\xB0\x89\xFF", 5));
	string_pop_back_sjis(&s);
	assert(str_is(s, "\x82\xA0\xB0", 3));
	string_pop_back_sjis(&s);
	string_pop_back_sjis(&s);
	assert(str_is(s, "", 0));
	free_string(s);
}

static void test_switch_back(void)
{
	sys4_set_string_charset(SYS4_CHARSET_SJIS);
	assert(sys4_get_string_charset() == SYS4_CHARSET_SJIS);
	assert(mbcs_count_char(QI_LA QI_LA) == 7);
	struct string *s = str("\x82\xA0");
	assert(string_get_char(s, 0) == 0xA082);
	EXPECT_ERROR(string_get_char(s, 99));
	free_string(s);
	s = str("");
	string_push_back(&s, 0xA3B0);
	assert(str_is(s, "\xB0", 1));
	free_string(s);
	char buf[64];
	int len = int_to_cstr(buf, sizeof buf, 12, 0, false, true);
	assert(len == 4 && !memcmp(buf, "\x82\x50\x82\x51", 5));

	// unknown values are rejected and keep the current rule
	sys4_set_string_charset((enum sys4_charset)7);
	assert(sys4_get_string_charset() == SYS4_CHARSET_SJIS);
}

static unsigned rng_state = 12345;
static unsigned rng(void)
{
	rng_state = rng_state * 1103515245u + 12345u;
	return rng_state >> 16;
}

static void test_sjis_differential(void)
{
	assert(sys4_get_string_charset() == SYS4_CHARSET_SJIS);
	char buf[17];
	for (int n = 0; n < 20000; n++) {
		int len = rng() % 17;
		for (int i = 0; i < len; i++)
			buf[i] = 1 + rng() % 255;
		buf[len] = '\0';
		assert(mbcs_count_char(buf) == sjis_count_char(buf));
		for (int i = -1; i <= 17; i++)
			assert(mbcs_index(buf, i) == sjis_index(buf, i));
	}
}

int main(void)
{
	sys_error_handler = on_error;
	assert(sys4_get_string_charset() == SYS4_CHARSET_SJIS);
	sys4_set_string_charset(SYS4_CHARSET_GBK);
	assert(sys4_get_string_charset() == SYS4_CHARSET_GBK);
	test_count_and_index();
	test_find_and_copy();
	test_push_pop_erase();
	test_get_char();
	test_set_char();
	test_numbers();
	test_sjis_helpers();
	test_switch_back();
	test_sjis_differential();
	printf("string_gbk: all tests passed\n");
	return 0;
}
