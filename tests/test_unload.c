#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <ttypt/ndx.h>

NDX_LISTENER(int, get_counter, int, dummy);

/* Helper: path without .so suffix */
#define MOD_PATH         "tests/mods/mod_unload"
#define MOD_PATH2        "tests/mods/mod_unload2"
#define MOD_CASCADE_P    "tests/mods/mod_cascade_parent"
#define MOD_CASCADE_C    "tests/mods/mod_cascade_child"
#define MOD_CLAIM_SIMPLE   "tests/mods/mod_claim_simple"
#define MOD_CASCADE_DEEP_A "tests/mods/mod_cascade_deep_a"

/* -------------------------------------------------------------------------
 * Basic load / unload
 * ------------------------------------------------------------------------- */

static void test_load_and_unload(void) {
	int r = ndx_load(MOD_PATH);
	assert(r == NDX_OK);

	/* call_count persists across ndx_shutdown/ndx_init due to RTLD_NODELETE.
	 * Use relative assertions so test order doesn't matter. */
	int v0 = 0;
	NDX_CALL(&v0, get_counter, 0); /* first call — remember baseline */
	int v1 = 0;
	NDX_CALL(&v1, get_counter, 0);
	assert(v1 == v0 + 1); /* each call increments by 1 */

	r = ndx_unload(MOD_PATH);
	assert(r == NDX_OK);

	int vz = 0;
	NDX_CALL(&vz, get_counter, 0);
	assert(vz == 0); /* no modules dispatching */

	printf("  test_load_and_unload: PASS\n");
}

/* -------------------------------------------------------------------------
 * Refcount: module stays alive until last holder unloads it
 * ------------------------------------------------------------------------- */

static void test_refcount(void) {
	assert(ndx_load(MOD_PATH) == NDX_OK);
	assert(ndx_load(MOD_PATH) == NDX_OK);

	int v;
	NDX_CALL(&v, get_counter, 0);
	assert(v > 0);

	/* First unload — refcount 2→1, module stays */
	assert(ndx_unload(MOD_PATH) == NDX_OK);
	NDX_CALL(&v, get_counter, 0);
	assert(v > 0);

	/* Second unload — refcount 1→0, module removed */
	assert(ndx_unload(MOD_PATH) == NDX_OK);
	v = 0;
	NDX_CALL(&v, get_counter, 0);
	assert(v == 0);

	printf("  test_refcount: PASS\n");
}

/* -------------------------------------------------------------------------
 * Unload on a path that was never loaded → NDX_ERR_NOTFOUND
 * ------------------------------------------------------------------------- */

static void test_unload_notfound(void) {
	int r = ndx_unload("tests/mods/nonexistent");
	assert(r == NDX_ERR_NOTFOUND);
	printf("  test_unload_notfound: PASS\n");
}

/* -------------------------------------------------------------------------
 * Double unload: after refcount hits 0, a second unload → NDX_ERR_NOTFOUND
 * ------------------------------------------------------------------------- */

static void test_double_unload(void) {
	assert(ndx_load(MOD_PATH) == NDX_OK);
	assert(ndx_unload(MOD_PATH) == NDX_OK);

	int r = ndx_unload(MOD_PATH);
	assert(r == NDX_ERR_NOTFOUND);

	printf("  test_double_unload: PASS\n");
}

/* -------------------------------------------------------------------------
 * Reload: module is re-installed in the same dispatch position
 * ------------------------------------------------------------------------- */

static void test_reload(void) {
	assert(ndx_load(MOD_PATH) == NDX_OK);

	/* With RTLD_NODELETE, call_count persists across tests */
	int v, v2;
	NDX_CALL(&v, get_counter, 0);
	assert(v > 0);
	NDX_CALL(&v2, get_counter, 0);
	assert(v2 == v + 1);

	int r = ndx_reload(MOD_PATH);
	assert(r == NDX_OK);

	/* Counter continues incrementing after reload */
	int v3;
	NDX_CALL(&v3, get_counter, 0);
	assert(v3 == v2 + 1);

	assert(ndx_unload(MOD_PATH) == NDX_OK);
	int vz = 0;
	NDX_CALL(&vz, get_counter, 0);
	assert(vz == 0);

	printf("  test_reload: PASS\n");
}

/* -------------------------------------------------------------------------
 * Reload on a path not currently loaded → NDX_ERR_NOTFOUND
 * ------------------------------------------------------------------------- */

static void test_reload_notfound(void) {
	int r = ndx_reload("tests/mods/nonexistent");
	assert(r == NDX_ERR_NOTFOUND);
	printf("  test_reload_notfound: PASS\n");
}

/* -------------------------------------------------------------------------
 * Reload after full unload → NDX_ERR_NOTFOUND
 * ------------------------------------------------------------------------- */

static void test_reload_after_unload(void) {
	assert(ndx_load(MOD_PATH) == NDX_OK);
	assert(ndx_unload(MOD_PATH) == NDX_OK);

	int r = ndx_reload(MOD_PATH);
	assert(r == NDX_ERR_NOTFOUND);

	printf("  test_reload_after_unload: PASS\n");
}

/* -------------------------------------------------------------------------
 * Reload preserves dispatch position
 *
 * Load mod_unload (counter++) then mod_unload2 (sentinel 99).
 * Last-write-wins dispatch → get_counter returns 99.
 * After reloading mod_unload it must remain before mod_unload2,
 * so get_counter still returns 99.
 * ------------------------------------------------------------------------- */

static void test_reload_position_preserved(void) {
	assert(ndx_load(MOD_PATH) == NDX_OK);
	assert(ndx_load(MOD_PATH2) == NDX_OK);

	int v;
	NDX_CALL(&v, get_counter, 0);
	assert(v == 99); /* mod_unload2 ran last */

	assert(ndx_reload(MOD_PATH) == NDX_OK);

	NDX_CALL(&v, get_counter, 0);
	assert(v == 99); /* mod_unload2 still last */

	assert(ndx_unload(MOD_PATH) == NDX_OK);
	NDX_CALL(&v, get_counter, 0);
	assert(v == 99); /* mod_unload2 alone */

	assert(ndx_unload(MOD_PATH2) == NDX_OK);
	v = 0;
	NDX_CALL(&v, get_counter, 0);
	assert(v == 0);

	printf("  test_reload_position_preserved: PASS\n");
}

/* -------------------------------------------------------------------------
 * Cascade unload: unloading a parent also removes its child
 *
 * mod_cascade_parent loads mod_cascade_child during ndx_install.
 * Because ndx_install runs before the parent is appended to the dispatch
 * list, the order is: child (55) → parent (77).  Parent runs last → 77.
 * Unloading the parent must cascade-remove the child too.
 * ------------------------------------------------------------------------- */

static void test_cascade_unload(void) {
	assert(ndx_load(MOD_CASCADE_P) == NDX_OK);

	int v;
	NDX_CALL(&v, get_counter, 0);
	assert(v == 77); /* parent ran last */

	assert(ndx_unload(MOD_CASCADE_P) == NDX_OK);

	v = 0;
	NDX_CALL(&v, get_counter, 0);
	assert(v == 0); /* both parent and child gone */

	printf("  test_cascade_unload: PASS\n");
}

/* -------------------------------------------------------------------------
 * Cascade with shared child: child loaded independently is not cascade-removed
 *
 * Load the child directly (refcount=1, parent_entry=NULL), then load the
 * parent (parent's ndx_install calls ndx_load(child) again → duplicate path,
 * child refcount→2, parent_entry stays NULL).
 * The cascade filter is parent_entry==parent_entry; since the child's
 * parent_entry is NULL the cascade does not touch it.
 * After parent unload: child refcount stays 2, still active.
 * Two explicit unloads bring it to 0 and remove it.
 * ------------------------------------------------------------------------- */

static void test_cascade_shared_child(void) {
	assert(ndx_load(MOD_CASCADE_C) == NDX_OK);   /* child refcount=1, parent_entry=NULL */
	assert(ndx_load(MOD_CASCADE_P) == NDX_OK);   /* child refcount→2 (duplicate); parent appended after */

	int v;
	NDX_CALL(&v, get_counter, 0);
	assert(v == 77); /* parent ran last */

	/* Unload parent — cascade does NOT touch the child (parent_entry==NULL).
	 * Parent is removed; child remains with refcount=2. */
	assert(ndx_unload(MOD_CASCADE_P) == NDX_OK);

	NDX_CALL(&v, get_counter, 0);
	assert(v == 55); /* child still dispatching, now runs last */

	/* Two explicit unloads to drain the refcount from 2 to 0 */
	assert(ndx_unload(MOD_CASCADE_C) == NDX_OK); /* refcount 2→1, stays */
	NDX_CALL(&v, get_counter, 0);
	assert(v == 55); /* still active */

	assert(ndx_unload(MOD_CASCADE_C) == NDX_OK); /* refcount 1→0, removed */
	v = 0;
	NDX_CALL(&v, get_counter, 0);
	assert(v == 0);

	printf("  test_cascade_shared_child: PASS\n");
}

/* -------------------------------------------------------------------------
 * Unload from wrong region: a claim-promoted module is stored under its
 * child region key.  ndx_unload from root builds a root-region key and
 * finds nothing → NDX_ERR_NOTFOUND.  This is the correct / intended behaviour:
 * the caller must be in the right region context to unload.  In practice a
 * module in the same child region (e.g. a sibling loaded into that region)
 * would call ndx_unload and it would succeed.  From the host (root) there is
 * no supported path to force-unload a claim-promoted module; ndx_shutdown()
 * cleans everything up unconditionally.
 *
 * Setup: register a permissive claim handler at root, then load
 * mod_claim_simple (ndx_claim=2).  Auto-claim fires; module is keyed under
 * the allocated child region, not root.
 * ------------------------------------------------------------------------- */

static int permissive_handler(const char *path, uint8_t req,
                               uint8_t *granted, void *ud) {
	(void)path; (void)ud;
	*granted = req;
	return NDX_OK;
}

static void test_unload_wrong_region(void) {
	/* Register claim handler at root so mod_claim_simple auto-claims */
	int r = ndx_require_claim(permissive_handler, NULL);
	assert(r == NDX_OK);

	r = ndx_load(MOD_CLAIM_SIMPLE);
	assert(r == NDX_OK);

	/* Module is keyed under its child region, not root.
	 * Calling ndx_unload from root must return NDX_ERR_NOTFOUND. */
	r = ndx_unload(MOD_CLAIM_SIMPLE);
	assert(r == NDX_ERR_NOTFOUND);

	printf("  test_unload_wrong_region: PASS\n");
	/* ndx_shutdown() in main cleans up */
}

/* -------------------------------------------------------------------------
 * Deep cascade unload: 3-level chain A → B → C
 *
 * Loading A triggers A's ndx_install which loads B; B's ndx_install loads C.
 * Install runs before append, so dispatch order is C→B→A (A appended last,
 * runs last, returns 11).
 * Unloading A must cascade through B (parent_entry=A) to C (parent_entry=B).
 * All three must be gone afterwards.
 * ------------------------------------------------------------------------- */

static void test_cascade_deep(void) {
	assert(ndx_load(MOD_CASCADE_DEEP_A) == NDX_OK);

	int v;
	NDX_CALL(&v, get_counter, 0);
	assert(v == 11); /* A ran last */

	assert(ndx_unload(MOD_CASCADE_DEEP_A) == NDX_OK);

	v = 0;
	NDX_CALL(&v, get_counter, 0);
	assert(v == 0); /* C, B, and A all gone */

	printf("  test_cascade_deep: PASS\n");
}

/* -------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */

int main(void) {
	printf("test_unload:\n");

	test_load_and_unload();
	ndx_shutdown(); ndx_init();

	test_refcount();
	ndx_shutdown(); ndx_init();

	test_unload_notfound();
	ndx_shutdown(); ndx_init();

	test_double_unload();
	ndx_shutdown(); ndx_init();

	test_reload();
	ndx_shutdown(); ndx_init();

	test_reload_notfound();
	ndx_shutdown(); ndx_init();

	test_reload_after_unload();
	ndx_shutdown(); ndx_init();

	test_reload_position_preserved();
	ndx_shutdown(); ndx_init();

	test_cascade_unload();
	ndx_shutdown(); ndx_init();

	test_cascade_shared_child();
	ndx_shutdown(); ndx_init();

	test_cascade_deep();
	ndx_shutdown(); ndx_init();

	test_unload_wrong_region();
	ndx_shutdown();

	printf("  all tests passed\n");
	return 0;
}
