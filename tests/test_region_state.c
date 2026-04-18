/*
 * test_region_state: tests for per-region module state.
 *
 * Tests:
 *   test_region_state_zeroed      — state fields start at zero
 *   test_region_state_independent — two regions share the same .so but have
 *                                   independent counters
 *   test_region_state_cleanup     — ndx_region_cleanup is called on unload
 */
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <ttypt/ndx.h>

NDX_DEF(int, rs_increment, int, dummy);
NDX_DEF(int, rs_get,       int, dummy);

#define MOD_RS "tests/mods/mod_region_state"

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* A claim handler that always grants exactly the requested bits. */
static int permissive_handler(const char *path, uint8_t req,
                               uint8_t *granted, void *ud) {
	(void)path; (void)ud;
	*granted = req;
	return NDX_OK;
}

/* -------------------------------------------------------------------------
 * test_region_state_zeroed
 *
 * Load the module and immediately read the counter — must be 0.
 * ------------------------------------------------------------------------- */
static void test_region_state_zeroed(void) {
	assert(ndx_load(MOD_RS) == NDX_OK);

	int v = -1;
	NDX_CALL(&v, rs_get, 0);
	assert(v == 0);

	printf("  test_region_state_zeroed: PASS\n");
}

/* -------------------------------------------------------------------------
 * test_region_state_independent
 *
 * Load mod_region_state twice with a permissive claim handler — each load
 * auto-claims its own child region, so the same .so gets two independent
 * state blocks.
 *
 * After loading, call rs_increment once.  Both instances run; each returns 1
 * (counter goes 0→1 in its own state).  Because the last-write wins in
 * NDX_CALL, the macro result is 1 — but crucially the second module's counter
 * is 1, not 2 (confirming they don't share state).
 * ------------------------------------------------------------------------- */
static void test_region_state_independent(void) {
	/* Enable claim gate — each load gets its own child region */
	assert(ndx_require_claim(permissive_handler, NULL) == NDX_OK);

	assert(ndx_load(MOD_RS) == NDX_OK);  /* child region A */
	assert(ndx_load(MOD_RS) == NDX_OK);  /* child region B */

	/* Increment once — both instances run, each increments its own counter */
	int v = 0;
	NDX_CALL(&v, rs_increment, 0);
	/* Last module returned 1 (its counter went 0→1) */
	assert(v == 1);

	/* Increment again — both instances run again, each goes to 2 */
	NDX_CALL(&v, rs_increment, 0);
	assert(v == 2);

	/* If state were shared, one instance would have reached 4 and the other 0.
	 * Since both are 2, the counters are independent. */

	printf("  test_region_state_independent: PASS\n");
}

/* -------------------------------------------------------------------------
 * test_region_state_cleanup
 *
 * Load the module, then unload it.  The rs_cleanup_called global in the .so
 * must be non-zero afterwards.
 *
 * Because RTLD_NODELETE keeps the .so mapped, the global survives unload and
 * we can read it via dlsym after ndx_unload.
 * ------------------------------------------------------------------------- */
static void test_region_state_cleanup(void) {
	assert(ndx_load(MOD_RS) == NDX_OK);

	/* Read the cleanup counter before unload */
	void *handle = dlopen("tests/mods/mod_region_state.so",
	                       RTLD_NOW | RTLD_NOLOAD);
	assert(handle);
	int *cleanup_ptr = (int *)dlsym(handle, "rs_cleanup_called");
	assert(cleanup_ptr);
	int before = *cleanup_ptr;

	assert(ndx_unload(MOD_RS) == NDX_OK);

	int after = *cleanup_ptr;
	assert(after == before + 1);

	dlclose(handle);
	printf("  test_region_state_cleanup: PASS\n");
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void) {
	printf("test_region_state:\n");
	ndx_init();

	test_region_state_zeroed();
	ndx_shutdown(); ndx_init();

	test_region_state_independent();
	ndx_shutdown(); ndx_init();

	test_region_state_cleanup();
	ndx_shutdown();

	printf("  all tests passed\n");
	return 0;
}
