/*
 * Single-step (EFLAGS.TF -> #DB) test.
 *
 * v86 historically logged "Not supported: trap flag" and stripped TF in
 * update_eflags, so setting the trap flag never produced a debug exception.
 * That broke gdb's next/step, debugging of dynamically-linked binaries, and
 * continuing past a breakpoint still armed at the stop address.
 *
 * This exercises the implemented behaviour:
 *   - setting TF raises #DB (vector 1) after the following instruction;
 *   - DR6.BS (bit 14) is set for a single-step #DB;
 *   - TF is cleared on handler entry, so the handler is not itself
 *     single-stepped (which is what makes the trap count finite), and TF is
 *     restored by iret so stepping continues until the handler clears it.
 *
 * It is a 32-bit companion to the single-step subtest of the upstream
 * (x86-64-only) x86/debug.c.
 */

#include "libcflat.h"
#include "desc.h"
#include "processor.h"

#define STEPS 4

static volatile unsigned int n;
static volatile int all_db;   /* every trap so far was vector DB_VECTOR */
static volatile int all_bs;   /* every trap so far had DR6.BS set */

static unsigned long get_dr6(void)
{
	unsigned long value;
	asm volatile("mov %%dr6,%0" : "=r"(value));
	return value;
}

static void handle_db(struct ex_regs *regs)
{
	if (regs->vector != DB_VECTOR)
		all_db = 0;
	if (!(get_dr6() & (1u << 14)))
		all_bs = 0;

	if (++n >= STEPS) {
		/* Stop single-stepping: clear TF in the flags iret will restore. */
		regs->rflags &= ~X86_EFLAGS_TF;
	}
}

int main(void)
{
	setup_idt();
	handle_exception(DB_VECTOR, handle_db);

	n = 0;
	all_db = 1;
	all_bs = 1;

	/*
	 * Set TF, then execute a run of NOPs. Every instruction after the popf
	 * traps (#DB) until handle_db has counted STEPS traps and clears TF. If
	 * TF were not cleared on handler entry the handler would single-step
	 * itself and n would never settle, so a finite n == STEPS also proves
	 * that property. More NOPs than STEPS are provided so the last ones run
	 * after TF is cleared and must not trap.
	 */
	asm volatile(
		"pushf\n\t"
		"pop %%eax\n\t"
		"or $0x100, %%eax\n\t"   /* set TF */
		"push %%eax\n\t"
		"popf\n\t"
		"nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
		"nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
		: : : "eax", "cc");

	report("single-step raised #DB exactly STEPS times", n == STEPS);
	report("single-step exception vector is #DB", all_db);
	report("single-step sets DR6.BS", all_bs);

	return report_summary();
}
