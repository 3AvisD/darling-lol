/* Host model of the shipped process-local Mach semaphore code in
 *   darling/src/libsystem_kernel/emulation/src/xnu_syscall/mach/impl/mach_traps.c
 *
 * The model does not re-implement the algorithm: extract-lsem.py copies the `lsem` definitions, the trap entry points
 * and the create/destroy wrappers verbatim out of the real source into lsem-shipped.inc, which is #included below.
 * Only the platform edges (raw syscalls, darlingserver RPCs, the league-game check, logging) are replaced by the
 * shims in this file.
 *
 * Build:
 *   python3 extract-lsem.py
 *   cc -O2 -pthread -Wall -o lsem-model lsem-model.c
 * Run ./lsem-model; the exit status is non-zero if any check failed or any counter below is non-zero. The last line
 * is the count the fix is judged by: spurious_success / lost_wakeup / steal / hang must all be zero.
 *
 * What it checks (see the local semaphore section of docs/STATUS.md):
 *   counting, signal_all, signal_thread, timedwait arithmetic, destroy waking waiters
 *   bug A: find/destroy race, slot reuse under the same name, token stealing by a stale waiter (deterministic and
 *          multithreaded)
 *   bug B: fork (the child must go to the server for inherited names and work locally for its own)
 *   conservative fallback: unknown names, other tasks, failed creates, the switch off
 *   combined wait_signal/timedwait_signal: XNU registration order, both local, one local, both server
 *   stress: stable names with a produced/consumed ledger, waiter+signaler pairs, create/destroy churn with name
 *           reuse, destroy-while-waiting rounds; a 60 s watchdog reports a hang
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------------------ machine-independent shims ---- */

typedef int kern_return_t;
typedef unsigned int mach_port_name_t;
typedef int clock_res_t;

#define KERN_SUCCESS 0
#define KERN_FAILURE 5
#define KERN_ABORTED 14
#define KERN_INVALID_VALUE 18
#define KERN_INVALID_NAME 15
#define KERN_TERMINATED 37
#define KERN_NOT_WAITING 48
#define KERN_OPERATION_TIMED_OUT 49
#define LINUX_EINTR 4
#define VISIBLE

#define __NR_read 0
#define __NR_write 1
#define __NR_open 2
#define __NR_close 3
#define __NR_access 21
#define __NR_getpid 39
#define __NR_futex 202
#define __NR_clock_gettime 228

#define MODEL_SELF_TASK 7u
/* What the "server" stubs answer for calls that would really go to darlingserver: a positive code no local path can
 * return, so the tests see exactly which trap fell back. */
#define MODEL_SERVER 0x7f00
#define MODEL_LOG_FD 42

static int model_switch_enabled = 1;   /* the /tmp/rclol-local-semaphores switch the game process would see */
static int model_create_fail;          /* make __darling_rpc_semaphore_create fail */
static int model_rpc_calls[7];         /* signal, signal_all, wait, wait_signal, timedwait, timedwait_signal, create */
static int model_rpc_wait_code = MODEL_SERVER;
static int model_rpc_signal_code = KERN_SUCCESS;
static int model_unimplemented;
static mach_port_name_t model_next_port_name = 0x1000;
static mach_port_name_t model_forced_name; /* the name the next create must return (port-name reuse) */
static char model_log[4096];
static size_t model_log_used;

static int model_access(const char* path)
{
	if (!model_switch_enabled)
		return -1;
	if (!strcmp(path, "/tmp/rclol-local-semaphores-all") || !strcmp(path, "/tmp/rclol-local-semaphores"))
		return 0;
	return -1;
}

static long model_syscall(long nr, ...)
{
	va_list ap;
	long r = -1;
	va_start(ap, nr);
	switch (nr) {
	case __NR_futex: {
		int* addr = va_arg(ap, int*);
		int op = va_arg(ap, int);
		int val = va_arg(ap, int);
		void* ts = va_arg(ap, void*);
		(void) va_arg(ap, unsigned long);
		(void) va_arg(ap, int);
		r = (long) syscall(SYS_futex, addr, op, val, ts, NULL, 0);
		break;
	}
	case __NR_clock_gettime: {
		int clk = va_arg(ap, int);
		struct timespec* ts = va_arg(ap, struct timespec*);
		r = clock_gettime(clk, ts);
		break;
	}
	case __NR_access:
		r = model_access(va_arg(ap, const char*));
		(void) va_arg(ap, int);
		break;
	case __NR_getpid:
		r = getpid();
		break;
	case __NR_open: {
		const char* path = va_arg(ap, const char*);
		(void) va_arg(ap, int);
		(void) va_arg(ap, int);
		r = strstr(path, "rclol-game-decisions.log") ? MODEL_LOG_FD : -1;
		break;
	}
	case __NR_write: {
		long fd = va_arg(ap, long);
		const char* buf = va_arg(ap, const char*);
		int len = va_arg(ap, int);
		if (fd == MODEL_LOG_FD && len > 0) {
			size_t room = sizeof(model_log) - 1 - model_log_used;
			if ((size_t) len > room)
				len = (int) room;
			memcpy(model_log + model_log_used, buf, (size_t) len);
			model_log_used += (size_t) len;
			model_log[model_log_used] = 0;
		}
		r = len;
		break;
	}
	case __NR_close:
		(void) va_arg(ap, long);
		r = 0;
		break;
	default:
		fprintf(stderr, "model_syscall: unhandled syscall %ld\n", nr);
		abort();
	}
	va_end(ap);
	return r;
}
#define LINUX_SYSCALL(nr, ...) model_syscall((nr), ##__VA_ARGS__)

static int model_sprintf(char* buf, const char* fmt, ...)
{
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(buf, 4096, fmt, ap);
	va_end(ap);
	return n;
}
static void model_printf(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}
#define __simple_sprintf model_sprintf
#define __simple_printf model_printf
#define UNIMPLEMENTED_TRAP() do { ++model_unimplemented; } while (0)

static int darling_is_league_game(void)
{
	return model_switch_enabled;
}

static mach_port_name_t mach_task_self(void)
{
	return MODEL_SELF_TASK;
}

static void dserver_retry_sleep(int attempt)
{
	(void) attempt;
}

static int slowwait_enabled(void)
{
	return 0;
}
static unsigned long long slowwait_now(void)
{
	return 0;
}
static void slowwait_log(const char* what, unsigned int name, unsigned long long start, void** frames, int option, int extra)
{
	(void) what; (void) name; (void) start; (void) frames; (void) option; (void) extra;
}

/* The darlingserver calls the local table has no entry for. A server-side wait would block, which is not modelled;
 * the code the stub returns is what the trap returns, and the tests use MODEL_SERVER to see the fallback. */
static kern_return_t dserver_rpc_semaphore_signal(uint32_t name)
{
	(void) name; ++model_rpc_calls[0]; return model_rpc_signal_code;
}
static kern_return_t dserver_rpc_semaphore_signal_all(uint32_t name)
{
	(void) name; ++model_rpc_calls[1]; return model_rpc_signal_code;
}
static kern_return_t dserver_rpc_semaphore_wait(uint32_t name)
{
	(void) name; ++model_rpc_calls[2]; return model_rpc_wait_code;
}
static kern_return_t dserver_rpc_semaphore_wait_signal(uint32_t wait_name, uint32_t signal_name)
{
	(void) wait_name; (void) signal_name; ++model_rpc_calls[3]; return model_rpc_wait_code;
}
static kern_return_t dserver_rpc_semaphore_timedwait(uint32_t wait_name, uint32_t sec, uint32_t nsec)
{
	(void) wait_name; (void) sec; (void) nsec; ++model_rpc_calls[4]; return model_rpc_wait_code;
}
static kern_return_t dserver_rpc_semaphore_timedwait_signal(uint32_t wait_name, uint32_t signal_name, uint32_t sec, uint32_t nsec)
{
	(void) wait_name; (void) signal_name; (void) sec; (void) nsec; ++model_rpc_calls[5]; return model_rpc_wait_code;
}
static kern_return_t __darling_rpc_semaphore_create(mach_port_name_t task, mach_port_name_t* semaphore, int policy, int value)
{
	(void) task; (void) policy; (void) value; ++model_rpc_calls[6];
	if (model_create_fail)
		return KERN_FAILURE;
	if (model_forced_name) {
		*semaphore = model_forced_name; /* port-name reuse, as the server is free to hand a freed name out again */
		model_forced_name = 0;
	} else {
		model_next_port_name += 4;
		*semaphore = model_next_port_name;
	}
	return KERN_SUCCESS;
}
static kern_return_t __darling_rpc_semaphore_destroy(mach_port_name_t task, mach_port_name_t semaphore)
{
	(void) task; (void) semaphore;
	return KERN_SUCCESS;
}

/* the trap entry points are defined in the shipped block below; lsem_post_signal calls this one */
kern_return_t semaphore_signal_trap_impl(mach_port_name_t signal_name);

/* ------------------------------------------------------------------------------- the shipped code ---------- */

#include "lsem-shipped.inc"

/* ------------------------------------------------------------------------------- test harness -------------- */

static long checks, failures;
static long spurious_success, lost_wakeup, steal, hang;
static long local_success, terminated, timed_out, server_fallbacks;
static volatile int model_done;

static void check(const char* what, int ok)
{
	++checks;
	if (!ok) {
		++failures;
		printf("  FAIL %s\n", what);
	}
}

static void note(const char* name)
{
	printf("[%s]\n", name);
}

/* silent create; the caller checks the result so a failure inside a loop prints once */
static mach_port_name_t model_create_named(mach_port_name_t name, int value, int* ok)
{
	mach_port_name_t got = 0;
	int kr;
	model_forced_name = name;
	kr = semaphore_create(MODEL_SELF_TASK, &got, 0, value);
	if (ok)
		*ok = (kr == KERN_SUCCESS && (name == 0 || got == name));
	return got;
}

static mach_port_name_t model_create(int value)
{
	int ok = 0;
	mach_port_name_t name = model_create_named(0, value, &ok);
	check("semaphore_create succeeds and returns a name", ok && name != 0 && name != LSEM_TOMB);
	return name;
}

static void model_destroy(mach_port_name_t name)
{
	semaphore_destroy(MODEL_SELF_TASK, name);
}

static int model_timedwait(mach_port_name_t name, long long ns)
{
	int kr = semaphore_timedwait_trap_impl(name, (unsigned int) (ns / 1000000000ll), (clock_res_t) (ns % 1000000000ll));
	if (kr == KERN_SUCCESS) ++local_success;
	else if (kr == KERN_OPERATION_TIMED_OUT) ++timed_out;
	else if (kr == MODEL_SERVER) ++server_fallbacks;
	return kr;
}

static void wait_until_waiters(mach_port_name_t name, int n)
{
	for (int i = 0; i < 20000; ++i) {
		struct lsem* s = lsem_find(name);
		if (s && __atomic_load_n(&s->waiters, __ATOMIC_SEQ_CST) >= n)
			return;
		usleep(200);
	}
	++hang;
	printf("  FAIL waiters of name %u never reached %d\n", name, n);
}

struct waiter_arg {
	mach_port_name_t name;
	mach_port_name_t second;   /* the signal side of the combined traps */
	long long timeout_ns;      /* -1: plain wait */
	int result;
};

static void* waiter_thread(void* p)
{
	struct waiter_arg* a = p;
	if (a->second)
		a->result = semaphore_wait_signal_trap_impl(a->name, a->second);
	else if (a->timeout_ns < 0)
		a->result = semaphore_wait_trap_impl(a->name);
	else
		a->result = semaphore_timedwait_trap_impl(a->name, (unsigned int) (a->timeout_ns / 1000000000ll),
				(clock_res_t) (a->timeout_ns % 1000000000ll));
	return 0;
}

static long long now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long) ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

static void model_clear_table(void)
{
	for (unsigned int i = 0; i < LSEM_SLOTS; ++i) {
		unsigned long long old = __atomic_load_n(&lsems[i].pv.pair, __ATOMIC_RELAXED);
		__atomic_store_n(&lsems[i].pv.pair, (((old >> 32) + 1) << 32) | 0, __ATOMIC_RELEASE);
		__atomic_store_n(&lsems[i].name, LSEM_TOMB, __ATOMIC_RELEASE);
		__atomic_store_n(&lsems[i].waiters, 0, __ATOMIC_RELAXED);
	}
	__atomic_store_n(&lsem_state, 1, __ATOMIC_RELEASE);
}

/* ------------------------------------------------------------------------------------------- tests -------- */

/* Runs in a fresh child before this process decided anything: the switch files are read at the first create. */
static void test_switch_off_child(void)
{
	pid_t pid = fork();
	int status = 0;
	note("switch off (fresh process)");
	if (pid == 0) {
		mach_port_name_t n = 0;
		model_switch_enabled = 0;
		semaphore_create(MODEL_SELF_TASK, &n, 0, 1);
		int local = lsem_find(n) != 0;
		int kr = semaphore_wait_trap_impl(n);
		_exit(local ? 1 : (kr == MODEL_SERVER ? 0 : 2));
	}
	waitpid(pid, &status, 0);
	check("with the switch off nothing is served locally", WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void test_decide_and_log(void)
{
	note("decide/log");
	check("the switch turns local semaphores on", lsem_decide() == 1);
	check("the decision is logged for the game process", strstr(model_log, "local_semaphores=1") != 0);
	{
		size_t before = model_log_used;
		check("deciding again returns the same answer", lsem_decide() == 1);
		check("and does not log a second line", model_log_used == before);
	}
	model_clear_table();
}

static void test_counting(void)
{
	mach_port_name_t a = model_create(0);
	mach_port_name_t b = model_create(2);
	note("counting");
	check("empty semaphore times out immediately", model_timedwait(a, 0) == KERN_OPERATION_TIMED_OUT);
	check("banked signals are consumed one by one", model_timedwait(b, 0) == KERN_SUCCESS
			&& model_timedwait(b, 0) == KERN_SUCCESS
			&& model_timedwait(b, 0) == KERN_OPERATION_TIMED_OUT);
	check("a signal on a local semaphore is local", semaphore_signal_trap_impl(b) == KERN_SUCCESS
			&& model_timedwait(b, 0) == KERN_SUCCESS);
	model_destroy(a);
	model_destroy(b);
}

static void test_signal_all(void)
{
	mach_port_name_t s = model_create(0);
	pthread_t t[4];
	struct waiter_arg a[4];
	note("signal_all");
	check("signal_all with nobody waiting is a no-op and banks nothing",
			semaphore_signal_all_trap_impl(s) == KERN_SUCCESS && model_timedwait(s, 0) == KERN_OPERATION_TIMED_OUT);
	for (int i = 0; i < 4; ++i) {
		a[i].name = s; a[i].second = 0; a[i].timeout_ns = 2000000000ll; a[i].result = -1;
		pthread_create(&t[i], 0, waiter_thread, &a[i]);
	}
	wait_until_waiters(s, 4);
	check("signal_all wakes all four waiters", semaphore_signal_all_trap_impl(s) == KERN_SUCCESS);
	for (int i = 0; i < 4; ++i)
		pthread_join(t[i], 0);
	check("every waiter returned SUCCESS", a[0].result == KERN_SUCCESS && a[1].result == KERN_SUCCESS
			&& a[2].result == KERN_SUCCESS && a[3].result == KERN_SUCCESS);
	check("signal_all banks nothing for later waiters", model_timedwait(s, 0) == KERN_OPERATION_TIMED_OUT);
	model_destroy(s);
}

static void test_signal_thread(void)
{
	mach_port_name_t s = model_create(0);
	pthread_t t;
	struct waiter_arg a = { .name = s, .second = 0, .timeout_ns = 2000000000ll, .result = -1 };
	note("signal_thread");
	check("signal_thread(any) with nobody waiting is KERN_NOT_WAITING", semaphore_signal_thread_trap_impl(s, 0) == KERN_NOT_WAITING);
	pthread_create(&t, 0, waiter_thread, &a);
	wait_until_waiters(s, 1);
	check("signal_thread(any) wakes one waiter", semaphore_signal_thread_trap_impl(s, 0) == KERN_SUCCESS);
	pthread_join(t, 0);
	check("the waiter returned SUCCESS", a.result == KERN_SUCCESS);
	check("signal_thread with a real thread is KERN_NOT_WAITING", semaphore_signal_thread_trap_impl(s, 0x1234) == KERN_NOT_WAITING);
	check("and banks nothing", model_timedwait(s, 0) == KERN_OPERATION_TIMED_OUT);
	model_destroy(s);
}

static void test_destroy_wakes(void)
{
	mach_port_name_t n = model_create(0);
	pthread_t t[3];
	struct waiter_arg a[3];
	note("destroy");
	for (int i = 0; i < 3; ++i) {
		a[i].name = n; a[i].second = 0; a[i].timeout_ns = 2000000000ll; a[i].result = -1;
		pthread_create(&t[i], 0, waiter_thread, &a[i]);
	}
	wait_until_waiters(n, 3);
	model_destroy(n);
	for (int i = 0; i < 3; ++i)
		pthread_join(t[i], 0);
	check("all three waiters got KERN_TERMINATED", a[0].result == KERN_TERMINATED && a[1].result == KERN_TERMINATED
			&& a[2].result == KERN_TERMINATED);
	check("the destroyed name goes to the server afterwards", semaphore_wait_trap_impl(n) == MODEL_SERVER);
}

static void test_find_destroy_race(void)
{
	mach_port_name_t n = model_create(0);
	struct lsem* s = lsem_find(n);
	unsigned int gen = lsem_capture(s);
	int kr;
	note("find/destroy race (bug A)");
	check("the object is found", s != 0 && s->name == n);
	model_destroy(n);
	kr = lsem_wait(s, n, gen, 0, 0);
	check("a wait that captured the object before the destroy is KERN_TERMINATED", kr == KERN_TERMINATED);
	if (kr == KERN_SUCCESS) ++spurious_success;
	check("a signal that captured the object before the destroy is KERN_TERMINATED", lsem_signal(s, n, gen) == KERN_TERMINATED);
	check("signal_all likewise", lsem_signal_all(s, n, gen) == KERN_TERMINATED);
	check("signal_thread likewise", lsem_signal_thread(s, n, gen, 0) == KERN_TERMINATED);
}

/* A trap that resolves the slot while a destroy is in flight sees the generation and the tombstone value the
 * destroy wrote (its two stores are the pair and the name, and another CPU can see the pair first). It must report
 * KERN_TERMINATED, never take the tombstone for a token. Reading the slot right after the destroy is exactly the
 * state such a trap sees; the check drives the *shipped* destroy, so it cannot pass on a positive tombstone. */
static void test_inflight_destroy(void)
{
	mach_port_name_t n = model_create(0);
	struct lsem* s = lsem_find(n);
	unsigned long long pair;
	unsigned int gen;
	int kr;
	note("destroy in flight: the tombstone value must never be a token");
	check("the object is found", s != 0);
	semaphore_signal_trap_impl(n);                 /* a real token is banked */
	model_destroy(n);                              /* gen+1 and the tombstone value, then the name */
	pair = __atomic_load_n(&s->pv.pair, __ATOMIC_SEQ_CST);
	check("the destroy's tombstone value is negative so no token take can accept it",
			(int) (unsigned int) pair < 0);
	/* Simulate what a trap that resolved the slot during the destroy's two stores sees: the pair is the destroy's,
	 * the name is still the one its own load returned (before the destroy's name store landed). */
	__atomic_store_n(&s->name, n, __ATOMIC_RELEASE);
	gen = lsem_capture(s);                          /* what that trap would capture */
	kr = lsem_wait(s, n, gen, 0, 0);
	check("the wait reports KERN_TERMINATED instead of consuming the tombstone", kr == KERN_TERMINATED);
	if (kr == KERN_SUCCESS) ++spurious_success;
	check("a signal on the tombstone value is KERN_TERMINATED", lsem_signal(s, n, gen) == KERN_TERMINATED);
	check("signal_all likewise", lsem_signal_all(s, n, gen) == KERN_TERMINATED);
	check("signal_thread likewise", lsem_signal_thread(s, n, gen, 0) == KERN_TERMINATED);
	model_destroy(n);
}

/* Fill the table except one slot, then create/destroy/re-create the same name: every generation gets the same slot.
 * A waiter that resolved the previous generation must not consume the new object's token (bug A, second half), and
 * the tombstone sentinel must never be reported as a success. */
#define STEAL_ROUNDS 400
static void test_slot_reuse_steal(void)
{
	unsigned int i;
	const mach_port_name_t name = 0x900000u;
	long bad = 0, rounds = 0;
	note("slot reuse / token steal (bug A)");
	model_clear_table();
	for (i = 0; i < LSEM_SLOTS - 1; ++i)
		lsem_insert(0x100000u + i * 8u, 0);
	check("the table is full except one slot", lsem_find(0x100000u) != 0 && lsem_find(0x100000u + (LSEM_SLOTS - 2) * 8u) != 0);
	for (int round = 0; round < STEAL_ROUNDS; ++round) {
		struct lsem* s;
		unsigned int gen, tomb_gen;
		int kr, kr2, kr3, ok1 = 0, ok2 = 0;
		mach_port_name_t created = model_create_named(name, 0, &ok1);
		if (!ok1) { ++bad; break; }
		s = lsem_find(created);
		if (!s) { ++bad; break; }
		gen = lsem_capture(s);
		model_destroy(created);
		tomb_gen = lsem_capture(s);                     /* the generation the tombstone holds */
		model_create_named(name, 0, &ok2);              /* same slot, new generation, no token */
		if (!ok2 || lsem_find(name) != s) { ++bad; break; }
		semaphore_signal_trap_impl(name);               /* the new object now has exactly one token */
		kr = lsem_wait(s, name, gen, 0, 0);             /* the stale waiter of the pre-destroy generation */
		kr3 = lsem_wait(s, name, tomb_gen, 0, 0);       /* a trap that saw the destroy's tombstone generation */
		if (kr == KERN_SUCCESS || kr3 == KERN_SUCCESS) {
			++spurious_success;
			++bad;
		}
		kr2 = model_timedwait(name, 0);                 /* the new object's token must still be there */
		if (kr2 != KERN_SUCCESS)
			++bad;
		model_destroy(name);
		++rounds;
	}
	check("400 re-use rounds: no stale success, no stolen token", bad == 0 && rounds == STEAL_ROUNDS);
	model_clear_table();
}

static void test_fork(void)
{
	pid_t pid;
	int status = 0;
	note("fork (bug B)");
	{
		mach_port_name_t n = model_create(1);
		int kr;
		pid = fork();
		if (pid == 0) {
			__darling_local_semaphores_after_fork();
			/* the inherited name must go to the server now... */
			int inherited = semaphore_wait_trap_impl(n) == MODEL_SERVER ? 0 : 1;
			/* ...and the child's own semaphores must work locally */
			mach_port_name_t own = 0;
			semaphore_create(MODEL_SELF_TASK, &own, 0, 1);
			int mine = semaphore_wait_trap_impl(own) == KERN_SUCCESS ? 0 : 1;
			_exit(inherited | (mine << 1));
		}
		waitpid(pid, &status, 0);
		check("the child sends inherited names to the server and serves its own locally",
				WIFEXITED(status) && WEXITSTATUS(status) == 0);
		kr = model_timedwait(n, 0);
		check("the parent's local entry still has its token", kr == KERN_SUCCESS);
		model_destroy(n);
	}
	{
		/* the child's own table must really be empty: it may not see the parent's entries at all */
		mach_port_name_t n = model_create(1);
		pid = fork();
		if (pid == 0) {
			__darling_local_semaphores_after_fork();
			_exit(lsem_find(n) == 0 ? 0 : 1);
		}
		waitpid(pid, &status, 0);
		check("the child's table has no inherited entry", WIFEXITED(status) && WEXITSTATUS(status) == 0);
		model_destroy(n);
	}
}

static void test_fallback(void)
{
	mach_port_name_t unknown = 0x707000u;
	mach_port_name_t other = 0;
	int signals_before = model_rpc_calls[0];
	note("fallback");
	check("an unknown name goes to the server", semaphore_wait_trap_impl(unknown) == MODEL_SERVER);
	check("signal too", semaphore_signal_trap_impl(unknown) == KERN_SUCCESS && model_rpc_calls[0] > signals_before);
	check("signal_all too", semaphore_signal_all_trap_impl(unknown + 4) == KERN_SUCCESS);
	check("timedwait too", semaphore_timedwait_trap_impl(unknown, 0, 1000000) == MODEL_SERVER);
	check("wait_signal too", semaphore_wait_signal_trap_impl(unknown, unknown + 1) == MODEL_SERVER);
	check("timedwait_signal too", semaphore_timedwait_signal_trap_impl(unknown, unknown + 1, 0, 1000000) == MODEL_SERVER);
	check("signal_thread (no server support) stays unimplemented", semaphore_signal_thread_trap_impl(unknown, 0) == KERN_FAILURE);
	check("semaphore_create for another task succeeds server-side",
			semaphore_create(99u, &other, 0, 1) == KERN_SUCCESS && lsem_find(other) == 0);
	check("its wait goes to the server", semaphore_wait_trap_impl(other) == MODEL_SERVER);
	model_create_fail = 1;
	{
		mach_port_name_t failed = 0;
		check("a failed create fails", semaphore_create(MODEL_SELF_TASK, &failed, 0, 1) != KERN_SUCCESS);
		check("and leaves no entry behind", lsem_find(failed) == 0);
	}
	model_create_fail = 0;
	{
		/* only the signal side is local: it is signalled locally, the wait stays in the server */
		mach_port_name_t local_signal = model_create(0);
		int kr = semaphore_wait_signal_trap_impl(unknown, local_signal);
		check("wait_signal(server wait, local signal) signals locally", kr == MODEL_SERVER
				&& model_timedwait(local_signal, 0) == KERN_SUCCESS);
		model_destroy(local_signal);
	}
	{
		/* only the wait side is local: the signal goes to the server, the wait keeps its token */
		mach_port_name_t local_wait = model_create(1);
		int before = model_rpc_calls[0];
		int kr = semaphore_wait_signal_trap_impl(local_wait, unknown);
		check("wait_signal(local wait, server signal) keeps its local token", kr == KERN_SUCCESS);
		check("and signalled the server side", model_rpc_calls[0] == before + 1);
		model_destroy(local_wait);
	}
}

static void test_combined_local(void)
{
	mach_port_name_t w = model_create(0);
	mach_port_name_t s = model_create(0);
	pthread_t t;
	struct waiter_arg a = { .name = w, .second = s, .timeout_ns = -1, .result = -2 };
	note("combined traps, both local");
	pthread_create(&t, 0, waiter_thread, &a);
	wait_until_waiters(w, 1);
	check("the wait side registered itself", __atomic_load_n(&lsem_find(w)->waiters, __ATOMIC_SEQ_CST) >= 1);
	check("and the signal side was signalled", semaphore_timedwait_trap_impl(s, 0, 0) == KERN_SUCCESS);
	check("the combined wait has not returned yet", __atomic_load_n(&a.result, __ATOMIC_SEQ_CST) == -2);
	semaphore_signal_trap_impl(w);
	pthread_join(t, 0);
	check("the combined wait then returned SUCCESS", a.result == KERN_SUCCESS);
	model_destroy(w);
	model_destroy(s);
}

/* XNU registers the wait and only then signals the signal semaphore. Observable: the signal semaphore has a token
 * while the wait side's registration is already visible. */
static void test_combined_order(void)
{
	mach_port_name_t w = model_create(0);
	mach_port_name_t s = model_create(0);
	struct lsem* ws = lsem_find(w);
	struct lsem* ss = lsem_find(s);
	pthread_t t;
	struct waiter_arg a = { .name = w, .second = s, .timeout_ns = -1, .result = -2 };
	int token_before_registration = 0, saw_token = 0;
	note("combined traps, XNU registration order");
	pthread_create(&t, 0, waiter_thread, &a);
	for (int i = 0; i < 20000; ++i) {
		if (__atomic_load_n(&ss->pv.w.value, __ATOMIC_SEQ_CST) > 0) {
			saw_token = 1;
			if (__atomic_load_n(&ws->waiters, __ATOMIC_SEQ_CST) < 1)
				token_before_registration = 1;
			break;
		}
		if (__atomic_load_n(&a.result, __ATOMIC_SEQ_CST) != -2)
			break;
		usleep(50);
	}
	semaphore_signal_trap_impl(w);
	pthread_join(t, 0);
	check("the signal side saw its token", saw_token);
	check("the signal was issued only after the wait registered itself", !token_before_registration);
	check("the combined wait returned SUCCESS", a.result == KERN_SUCCESS);
	model_destroy(w);
	model_destroy(s);
}

static void test_timeout(void)
{
	mach_port_name_t n = model_create(0);
	long long t0, dt;
	note("timeout arithmetic");
	t0 = now_ns();
	check("timedwait(0,0) is a non-blocking poll", model_timedwait(n, 0) == KERN_OPERATION_TIMED_OUT);
	check("and returns immediately", now_ns() - t0 < 5000000);
	t0 = now_ns();
	check("timedwait(5ms) times out", model_timedwait(n, 5000000) == KERN_OPERATION_TIMED_OUT);
	dt = now_ns() - t0;
	check("after roughly 5ms", dt >= 4000000 && dt < 500000000);
	check("nsec >= 1e9 is KERN_INVALID_VALUE", semaphore_timedwait_trap_impl(n, 0, 1000000000) == KERN_INVALID_VALUE);
	check("negative nsec is KERN_INVALID_VALUE", semaphore_timedwait_trap_impl(n, 0, -1) == KERN_INVALID_VALUE);
	{
		pthread_t t;
		struct waiter_arg a = { .name = n, .second = 0, .timeout_ns = 30000000ll, .result = -1 };
		pthread_create(&t, 0, waiter_thread, &a);
		usleep(2000);
		semaphore_signal_trap_impl(n);
		pthread_join(t, 0);
		check("a signal before the deadline wins over the timeout", a.result == KERN_SUCCESS);
	}
	model_destroy(n);
}

/* ------------------------------------------------------------------------------- multithreaded stress ------ */

#define STRESS_THREADS 8
#define STRESS_NAMES 24
#define STRESS_OPS 10000

static unsigned int stress_names[STRESS_NAMES];
static long produced[STRESS_NAMES], consumed[STRESS_NAMES];

static void* stress_accounting(void* p)
{
	unsigned int seed = (unsigned int) (uintptr_t) p;
	for (int i = 0; i < STRESS_OPS; ++i) {
		unsigned int which = rand_r(&seed) % STRESS_NAMES;
		switch (rand_r(&seed) % 5) {
		case 0: case 1:
			if (semaphore_signal_trap_impl(stress_names[which]) == KERN_SUCCESS)
				__atomic_add_fetch(&produced[which], 1, __ATOMIC_RELAXED);
			break;
		case 2:
			if (model_timedwait(stress_names[which], 100000) == KERN_SUCCESS)
				__atomic_add_fetch(&consumed[which], 1, __ATOMIC_RELAXED);
			break;
		case 3:
			/* a wait that only blocks if a signal raced in; the produced/consumed ledger counts it when it
			 * succeeds, so a success without a token is caught below */
			if (semaphore_timedwait_trap_impl(stress_names[which], 0, 1000) == KERN_SUCCESS)
				__atomic_add_fetch(&consumed[which], 1, __ATOMIC_RELAXED);
			break;
		case 4:
			if (model_timedwait(stress_names[which], 0) == KERN_SUCCESS)
				__atomic_add_fetch(&consumed[which], 1, __ATOMIC_RELAXED);
			break;
		}
	}
	return 0;
}

static void test_stress_accounting(void)
{
	pthread_t t[STRESS_THREADS];
	long bad = 0;
	note("stress: stable names, produced/consumed ledger");
	for (int i = 0; i < STRESS_NAMES; ++i) {
		stress_names[i] = 0x200000u + (unsigned) i * 4u;
		model_create_named(stress_names[i], 0, 0);
		produced[i] = consumed[i] = 0;
	}
	for (int i = 0; i < STRESS_THREADS; ++i)
		pthread_create(&t[i], 0, stress_accounting, (void*) (uintptr_t) (i + 1));
	for (int i = 0; i < STRESS_THREADS; ++i)
		pthread_join(t[i], 0);
	for (int i = 0; i < STRESS_NAMES; ++i)
		if (consumed[i] > produced[i])
			++bad;
	check("no name consumed a token that was never produced (no spurious success)", bad == 0);
	if (bad)
		spurious_success += bad;
	for (int i = 0; i < STRESS_NAMES; ++i)
		model_destroy(stress_names[i]);
}

#define WAKE_ROUNDS 300
static void test_wake_pairs(void)
{
	mach_port_name_t n = model_create(0);
	long lost = 0;
	note("stress: waiter/signaler pairs");
	for (int i = 0; i < WAKE_ROUNDS; ++i) {
		pthread_t t;
		struct waiter_arg a = { .name = n, .second = 0, .timeout_ns = 1000000000ll, .result = -1 };
		pthread_create(&t, 0, waiter_thread, &a);
		wait_until_waiters(n, 1);
		semaphore_signal_trap_impl(n);
		pthread_join(t, 0);
		if (a.result != KERN_SUCCESS)
			++lost;
	}
	lost_wakeup += lost;
	check("no signal was lost", lost == 0);
	model_destroy(n);
}

/* create/destroy churn with name reuse; hammer threads look for a stale wait that reports success between the
 * re-create and the first signal. Each churn thread owns two names, and the window flag is per name: inside a
 * name's window that object's value is zero and nobody signals it, so a successful wait there can only have taken
 * a token that was never produced (a stale waiter consuming the re-created object's token or the tombstone). */
#define CHURN_NAMES 8
#define CHURN_THREADS 4
#define CHURN_ROUNDS 3000
static unsigned int churn_names[CHURN_NAMES];
static volatile int churn_window[CHURN_NAMES];

static void* churn_thread(void* p)
{
	unsigned int index = (unsigned int) (uintptr_t) p; /* 0..CHURN_THREADS-1 */
	unsigned int seed = index * 2654435761u + 1u;
	for (int i = 0; i < CHURN_ROUNDS; ++i) {
		unsigned int which = index * 2u + (unsigned int) (rand_r(&seed) & 1u);
		mach_port_name_t name = churn_names[which];
		int ok = 0;
		__atomic_store_n(&churn_window[which], 1, __ATOMIC_SEQ_CST);
		model_destroy(name);
		model_create_named(name, 0, &ok);   /* same port name handed out again */
		for (volatile int spin = 0; spin < 2000; ++spin)
			;
		__atomic_store_n(&churn_window[which], 0, __ATOMIC_SEQ_CST);
		semaphore_signal_trap_impl(name);
		semaphore_signal_trap_impl(name);
		(void) model_timedwait(name, 0);
		(void) semaphore_signal_all_trap_impl(name);
		/* leave the object empty: the next window assumes no token exists when it opens */
		while (semaphore_timedwait_trap_impl(name, 0, 0) == KERN_SUCCESS)
			;
	}
	return 0;
}

static void* churn_hammer(void* p)
{
	unsigned int seed = (unsigned int) (uintptr_t) p;
	for (int i = 0; i < CHURN_ROUNDS * 4; ++i) {
		unsigned int which = rand_r(&seed) % CHURN_NAMES;
		mach_port_name_t name = churn_names[which];
		struct lsem* s;
		unsigned int gen;
		int open_before, open_after;
		if (!__atomic_load_n(&churn_window[which], __ATOMIC_SEQ_CST))
			continue;
		s = lsem_find(name);
		if (!s)
			continue;
		gen = lsem_capture(s);
		open_before = __atomic_load_n(&churn_window[which], __ATOMIC_SEQ_CST);
		/* the object may have been destroyed and re-created between the find and the wait; its re-created
		 * generation starts empty and is only signalled after the window closes */
		if (lsem_wait(s, name, gen, 0, 0) == KERN_SUCCESS) {
			open_after = __atomic_load_n(&churn_window[which], __ATOMIC_SEQ_CST);
			if (open_before && open_after)
				__atomic_add_fetch(&steal, 1, __ATOMIC_RELAXED);
		}
	}
	return 0;
}

static void test_stress_churn(void)
{
	pthread_t t[CHURN_THREADS], h[3];
	long steal_before = steal;
	note("stress: create/destroy churn, name reuse, stale waiters");
	for (int i = 0; i < CHURN_NAMES; ++i) {
		churn_names[i] = 0x300000u + (unsigned) i * 4u;
		model_create_named(churn_names[i], 0, 0);
		churn_window[i] = 0;
	}
	for (int i = 0; i < CHURN_THREADS; ++i)
		pthread_create(&t[i], 0, churn_thread, (void*) (uintptr_t) i);
	for (int i = 0; i < 3; ++i)
		pthread_create(&h[i], 0, churn_hammer, (void*) (uintptr_t) (i + 11));
	for (int i = 0; i < CHURN_THREADS; ++i)
		pthread_join(t[i], 0);
	for (int i = 0; i < 3; ++i)
		pthread_join(h[i], 0);
	check("no stale waiter consumed a token of a reused slot", steal == steal_before);
	for (int i = 0; i < CHURN_NAMES; ++i)
		model_destroy(churn_names[i]);
}

#define DESTROY_ROUNDS 300
static void test_destroy_while_waiting(void)
{
	mach_port_name_t n;
	long bad = 0, stolen = 0;
	note("stress: destroy while waiting");
	for (int round = 0; round < DESTROY_ROUNDS; ++round) {
		pthread_t t[3];
		struct waiter_arg a[3];
		n = model_create(0);
		for (int i = 0; i < 3; ++i) {
			a[i].name = n; a[i].second = 0; a[i].timeout_ns = 2000000000ll; a[i].result = -1;
			pthread_create(&t[i], 0, waiter_thread, &a[i]);
		}
		wait_until_waiters(n, 3);
		model_destroy(n);
		for (int i = 0; i < 3; ++i)
			pthread_join(t[i], 0);
		if (a[0].result != KERN_TERMINATED || a[1].result != KERN_TERMINATED || a[2].result != KERN_TERMINATED)
			++bad;
	}
	check("every waiter of a destroyed semaphore got KERN_TERMINATED", bad == 0);
	bad = 0;
	for (int round = 0; round < DESTROY_ROUNDS; ++round) {
		pthread_t t;
		struct waiter_arg a = { .name = 0, .second = 0, .timeout_ns = 2000000000ll, .result = -1 };
		int ok = 0;
		n = model_create(0);
		a.name = n;
		pthread_create(&t, 0, waiter_thread, &a);
		wait_until_waiters(n, 1);
		model_destroy(n);
		model_create_named(n, 0, &ok);  /* the same port name again: a different object */
		if (!ok)
			++bad;
		semaphore_signal_trap_impl(n);  /* and its own token */
		pthread_join(t, 0);
		if (a.result == KERN_SUCCESS)
			++stolen;                   /* the old waiter consumed the new object's token */
		if (model_timedwait(n, 0) != KERN_SUCCESS)
			++bad;                      /* or the token is gone */
		model_destroy(n);
	}
	check("a waiter of a destroyed object never takes the re-created object's token", stolen == 0);
	check("the re-created object kept its token", bad == 0);
	steal += stolen;
}

static void* watchdog(void* p)
{
	(void) p;
	for (int i = 0; i < 600; ++i) {
		if (__atomic_load_n(&model_done, __ATOMIC_ACQUIRE))
			return 0;
		usleep(100000);
	}
	++hang;
	printf("LSEM_MODEL HANG: no progress for 60 s\n");
	_exit(9);
}

int main(void)
{
	pthread_t wd;
	setvbuf(stdout, NULL, _IONBF, 0);
	pthread_create(&wd, 0, watchdog, 0);
	pthread_detach(wd);
	printf("LSEM_MODEL slots=%u\n", (unsigned) LSEM_SLOTS);
	test_switch_off_child();
	test_decide_and_log();
	test_counting();
	test_signal_all();
	test_signal_thread();
	test_destroy_wakes();
	test_find_destroy_race();
	test_inflight_destroy();
	test_fork();
	test_timeout();
	test_combined_local();
	test_combined_order();
	test_stress_accounting();
	test_wake_pairs();
	test_stress_churn();
	test_destroy_while_waiting();
	test_fallback();
	test_slot_reuse_steal();
	__atomic_store_n(&model_done, 1, __ATOMIC_RELEASE);
	printf("LSEM_MODEL checks=%ld failed=%ld spurious_success=%ld lost_wakeup=%ld steal=%ld hang=%ld "
	       "local_success=%ld terminated=%ld timed_out=%ld server=%ld rpc=%d/%d/%d/%d/%d/%d/%d\n",
	       checks, failures, spurious_success, lost_wakeup, steal, hang,
	       local_success, terminated, timed_out, server_fallbacks,
	       model_rpc_calls[0], model_rpc_calls[1], model_rpc_calls[2], model_rpc_calls[3],
	       model_rpc_calls[4], model_rpc_calls[5], model_rpc_calls[6]);
	return failures != 0 || spurious_success != 0 || lost_wakeup != 0 || steal != 0 || hang != 0;
}
