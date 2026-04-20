/*
 * test_ptr_args — regression coverage for NDX_DECL-caller, pointer-arg,
 * pointer-return hooks dispatched via libndx's fast path.
 *
 * Reproduces the crash shape observed in site/mods/auth where:
 *   - auth.h uses NDX_DECL (static adapter with .call == NULL)
 *   - auth.c uses NDX_DEF (provides real .call + hook body, registered at load)
 *   - host-binary code calls call_get_session_user() via the DECL'd inline
 *
 * The NDX_DECL path forces ndx_call() to sync .call/.ret_size/.arg_size
 * from the canonical adapter at the first resolution (when reg->hook_id<0).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mods/ptr_args.h"

/* ptr_resolve_remote is defined by the caller module but NOT part of the
 * shared header (caller-module-owned hooks aren't exposed there, mirroring
 * poem.c's hooks not appearing in auth.h). Declare locally for the harness. */
NDX_DECL(const char *, ptr_resolve_remote, int, which);

static void
test_single_ptr_arg_ptr_ret(void)
{
	const char *u = call_ptr_lookup("tok-alice");
	assert(u != NULL);
	assert(strcmp(u, "alice") == 0);

	u = call_ptr_lookup("tok-bob");
	assert(u != NULL);
	assert(strcmp(u, "bob") == 0);

	u = call_ptr_lookup("tok-nonexistent");
	assert(u == NULL);

	/* NULL token — must not crash, must return NULL */
	u = call_ptr_lookup(NULL);
	assert(u == NULL);

	/* Empty string token — guarded by !*token in the module */
	u = call_ptr_lookup("");
	assert(u == NULL);

	printf("  test_single_ptr_arg_ptr_ret: PASS\n");
}

static void
test_repeated_calls_same_hook(void)
{
	for (int i = 0; i < 1000; i++) {
		const char *u = call_ptr_lookup("tok-carol");
		assert(u != NULL);
		assert(strcmp(u, "carol") == 0);
	}
	printf("  test_repeated_calls_same_hook: PASS\n");
}

static void
test_alternating_hooks(void)
{
	/* Forces reg != ndx_last_reg every iteration. */
	for (int i = 0; i < 200; i++) {
		const char *u = call_ptr_lookup("tok-alice");
		assert(u && strcmp(u, "alice") == 0);

		int n = call_ptr_len("hello");
		assert(n == 5);

		u = call_ptr_lookup("tok-bob");
		assert(u && strcmp(u, "bob") == 0);

		n = call_ptr_len("hi");
		assert(n == 2);
	}
	printf("  test_alternating_hooks: PASS\n");
}

static void
test_multi_ptr_args(void)
{
	char buf[16];
	memset(buf, 0xAA, sizeof(buf));
	int r = call_ptr_copy(buf, "hello", sizeof(buf));
	assert(r == 5);
	assert(strcmp(buf, "hello") == 0);
	assert(buf[5] == '\0');

	printf("  test_multi_ptr_args: PASS\n");
}

/* Nested call: host invokes ptr_resolve which internally calls ptr_lookup.
 * Mirrors auth.h get_request_user -> call_get_session_user. */
static void
test_nested_ptr_call(void)
{
	const char *u = call_ptr_resolve(1);
	assert(u != NULL);
	assert(strcmp(u, "alice") == 0);

	u = call_ptr_resolve(2);
	assert(u != NULL);
	assert(strcmp(u, "bob") == 0);

	u = call_ptr_resolve(3);
	assert(u != NULL);
	assert(strcmp(u, "carol") == 0);

	u = call_ptr_resolve(99);
	assert(u == NULL);

	/* Stress the nested path repeatedly to hit fn_cache resolution
	 * cross-adapter state */
	for (int i = 0; i < 500; i++) {
		u = call_ptr_resolve(((i % 3) + 1));
		assert(u != NULL);
	}

	printf("  test_nested_ptr_call: PASS\n");
}

/* Cross-module nested call: module A's ptr_resolve_remote calls into
 * module B's ptr_lookup. Mirrors the poem.so -> auth.so -> auth.so chain. */
static void
test_cross_module_nested_call(void)
{
	const char *u = call_ptr_resolve_remote(1);
	assert(u != NULL);
	assert(strcmp(u, "alice") == 0);

	u = call_ptr_resolve_remote(2);
	assert(u != NULL);
	assert(strcmp(u, "bob") == 0);

	/* Hammer it. */
	for (int i = 0; i < 500; i++) {
		u = call_ptr_resolve_remote(((i % 3) + 1));
		assert(u != NULL);
	}
	printf("  test_cross_module_nested_call: PASS\n");
}

int
main(void)
{
	printf("test_ptr_args:\n");

	/* Before loading: call must not crash; returns 0-init. */
	const char *u = call_ptr_lookup("tok-alice");
	assert(u == NULL);

	/* Load module B (provides ptr_lookup/len/copy/resolve). */
	int rc = ndx_load("./tests/mods/mod_ptr_args");
	assert(rc == 0);

	/* Load module A (provides ptr_resolve_remote; uses ptr_lookup). */
	rc = ndx_load("./tests/mods/mod_ptr_args_caller");
	assert(rc == 0);

	test_single_ptr_arg_ptr_ret();
	test_repeated_calls_same_hook();
	test_alternating_hooks();
	test_multi_ptr_args();
	test_nested_ptr_call();
	test_cross_module_nested_call();

	printf("  all tests passed\n");
	return 0;
}
