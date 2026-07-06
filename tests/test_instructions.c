/* Tests for initialize_instructions(): instruction widths (ip_inc) must
 * match the declared argument count for the opcodes whose encoding changed
 * in AIN v11+ (NEW, CALLHLL, S_MOD, OBJSWAP, DG_STR_TO_METHOD).
 *
 * The VM advances the instruction pointer by ip_inc; if ip_inc disagrees
 * with nr_args the bytecode stream desyncs (v14 games crash immediately).
 * Also verifies that switching back to a pre-v11 version restores the
 * original widths (initialize_instructions may be called more than once).
 */
#include <assert.h>
#include <stdio.h>

#include "system4/instructions.h"

static void check(enum opcode op, const char *name, int expect_nr_args)
{
	const struct instruction *in = &instructions[op];
	if (in->nr_args != expect_nr_args) {
		fprintf(stderr, "%s: nr_args=%d, expected %d\n",
		        name, in->nr_args, expect_nr_args);
		assert(0);
	}
	int expect_width = 2 + in->nr_args * 4;
	if (in->ip_inc != expect_width) {
		fprintf(stderr, "%s: ip_inc=%d, expected %d (2 + %d*4)\n",
		        name, in->ip_inc, expect_width, in->nr_args);
		assert(0);
	}
}

static void check_v11plus(void)
{
	check(NEW, "NEW", 2);
	check(CALLHLL, "CALLHLL", 3);
	check(S_MOD, "S_MOD", 1);
	check(OBJSWAP, "OBJSWAP", 1);
	check(DG_STR_TO_METHOD, "DG_STR_TO_METHOD", 1);
}

static void check_prev11(void)
{
	check(NEW, "NEW", 0);
	check(CALLHLL, "CALLHLL", 2);
	check(S_MOD, "S_MOD", 0);
	check(OBJSWAP, "OBJSWAP", 0);
	check(DG_STR_TO_METHOD, "DG_STR_TO_METHOD", 0);
}

int main(void)
{
	// v14 (Dohna Dohna)
	initialize_instructions(14);
	check_v11plus();
	printf("v14 widths: OK\n");

	// v11 boundary
	initialize_instructions(11);
	check_v11plus();
	printf("v11 widths: OK\n");

	// switching back must fully restore pre-v11 widths
	initialize_instructions(4);
	check_prev11();
	printf("pre-v11 restore: OK\n");

	// and forward again
	initialize_instructions(14);
	check_v11plus();
	printf("v14 re-init: OK\n");

	printf("test_instructions: all tests passed\n");
	return 0;
}
