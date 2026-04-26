/*
 * bench_dispatch — microbenchmark for ndx_call hot path.
 *
 * Reports ns/call for:
 *   - direct hook invocation (single module, single hook)
 *   - alternating hook invocation (hook transition, memcpy path)
 *   - nested dispatch (same-module nested ndx_call)
 *   - cross-module nested dispatch (the SIGSEGV regression shape)
 *
 * Pair with: `make bench`. Runs with TSC/monotonic, N iterations each.
 * Not a regression test — ns/call numbers print for A/B comparison.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <ttypt/ndx.h>
#include "mods/ptr_args.h"

NDX_HOOK_DECL(const char *, ptr_resolve_remote, int, which);

#define ITER_WARM    10000
#define ITER_MEASURE 2000000

static inline uint64_t
now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void
bench_single(void)
{
	/* Warm: prime fn_cache, page cache. */
	for (int i = 0; i < ITER_WARM; i++) (void)ptr_lookup("tok-alice");

	uint64_t t0 = now_ns();
	const char *u;
	for (int i = 0; i < ITER_MEASURE; i++) {
		u = ptr_lookup("tok-alice");
	}
	uint64_t t1 = now_ns();
	(void)u;
	double ns = (double)(t1 - t0) / ITER_MEASURE;
	printf("  bench_single          %8.2f ns/call  (%d iters)\n", ns, ITER_MEASURE);
}

static void
bench_alternating(void)
{
	for (int i = 0; i < ITER_WARM; i++) {
		(void)ptr_lookup("tok-alice");
		(void)ptr_len("hi");
	}

	uint64_t t0 = now_ns();
	int sum = 0;
	for (int i = 0; i < ITER_MEASURE / 2; i++) {
		const char *u = ptr_lookup("tok-alice");
		int n = ptr_len("hi");
		sum += (u != NULL) + n;
	}
	uint64_t t1 = now_ns();
	(void)sum;
	/* Two calls per iter */
	double ns = (double)(t1 - t0) / ((ITER_MEASURE / 2) * 2);
	printf("  bench_alternating     %8.2f ns/call  (hook transition)\n", ns);
}

static void
bench_nested(void)
{
	for (int i = 0; i < ITER_WARM; i++) (void)ptr_resolve(1);

	uint64_t t0 = now_ns();
	const char *u;
	for (int i = 0; i < ITER_MEASURE; i++) {
		u = ptr_resolve(1);
	}
	uint64_t t1 = now_ns();
	(void)u;
	double ns = (double)(t1 - t0) / ITER_MEASURE;
	printf("  bench_nested          %8.2f ns/call  (same-module nested)\n", ns);
}

static void
bench_cross_module_nested(void)
{
	for (int i = 0; i < ITER_WARM; i++) (void)ptr_resolve_remote(1);

	uint64_t t0 = now_ns();
	const char *u;
	for (int i = 0; i < ITER_MEASURE; i++) {
		u = ptr_resolve_remote(1);
	}
	uint64_t t1 = now_ns();
	(void)u;
	double ns = (double)(t1 - t0) / ITER_MEASURE;
	printf("  bench_cross_mod_nested%8.2f ns/call  (poem->auth->auth shape)\n", ns);
}

int
main(void)
{
	printf("bench_dispatch:\n");

	int rc = ndx_load("./tests/mods/mod_ptr_args");
	assert(rc == 0);
	rc = ndx_load("./tests/mods/mod_ptr_args_caller");
	assert(rc == 0);

	bench_single();
	bench_alternating();
	bench_nested();
	bench_cross_module_nested();

	return 0;
}
