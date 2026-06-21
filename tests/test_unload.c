#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <ttypt/xy.h>

XY_LISTENER(int, get_counter, int, dummy);

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
	int r = xy_load(MOD_PATH);
	assert(r == XY_OK);

	/* call_count persists across xy_shutdown/xy_init due to RTLD_NODELETE.
	 * Use relative assertions so test order doesn't matter. */
	int v0 = 0;
	XY_CALL(&v0, get_counter, 0); /* first call — remember baseline */
	int v1 = 0;
	XY_CALL(&v1, get_counter, 0);
	assert(v1 == v0 + 1); /* each call increments by 1 */

	r = xy_unload(MOD_PATH);
	assert(r == XY_OK);

	int vz = 0;
	XY_CALL(&vz, get_counter, 0);
	assert(vz == 0); /* no modules dispatching */

	printf("  test_load_and_unload: PASS\n");
}

/* -------------------------------------------------------------------------
 * Refcount: module stays alive until last holder unloads it
 * ------------------------------------------------------------------------- */

static void test_refcount(void) {
	assert(xy_load(MOD_PATH) == XY_OK);
	assert(xy_load(MOD_PATH) == XY_OK);

	int v;
	XY_CALL(&v, get_counter, 0);
	assert(v > 0);

	/* First unload — refcount 2→1, module stays */
	assert(xy_unload(MOD_PATH) == XY_OK);
	XY_CALL(&v, get_counter, 0);
	assert(v > 0);

	/* Second unload — refcount 1→0, module removed */
	assert(xy_unload(MOD_PATH) == XY_OK);
	v = 0;
	XY_CALL(&v, get_counter, 0);
	assert(v == 0);

	printf("  test_refcount: PASS\n");
}

/* -------------------------------------------------------------------------
 * Unload on a path that was never loaded → XY_ERR_NOTFOUND
 * ------------------------------------------------------------------------- */

static void test_unload_notfound(void) {
	int r = xy_unload("tests/mods/nonexistent");
	assert(r == XY_ERR_NOTFOUND);
	printf("  test_unload_notfound: PASS\n");
}

/* -------------------------------------------------------------------------
 * Double unload: after refcount hits 0, a second unload → XY_ERR_NOTFOUND
 * ------------------------------------------------------------------------- */

static void test_double_unload(void) {
	assert(xy_load(MOD_PATH) == XY_OK);
	assert(xy_unload(MOD_PATH) == XY_OK);

	int r = xy_unload(MOD_PATH);
	assert(r == XY_ERR_NOTFOUND);

	printf("  test_double_unload: PASS\n");
}

/* -------------------------------------------------------------------------
 * Reload: module is re-installed in the same dispatch position
 * ------------------------------------------------------------------------- */

static void test_reload(void) {
	assert(xy_load(MOD_PATH) == XY_OK);

	/* With RTLD_NODELETE, call_count persists across tests */
	int v, v2;
	XY_CALL(&v, get_counter, 0);
	assert(v > 0);
	XY_CALL(&v2, get_counter, 0);
	assert(v2 == v + 1);

	int r = xy_reload(MOD_PATH);
	assert(r == XY_OK);

	/* Counter continues incrementing after reload */
	int v3;
	XY_CALL(&v3, get_counter, 0);
	assert(v3 == v2 + 1);

	assert(xy_unload(MOD_PATH) == XY_OK);
	int vz = 0;
	XY_CALL(&vz, get_counter, 0);
	assert(vz == 0);

	printf("  test_reload: PASS\n");
}

/* -------------------------------------------------------------------------
 * Reload on a path not currently loaded → XY_ERR_NOTFOUND
 * ------------------------------------------------------------------------- */

static void test_reload_notfound(void) {
	int r = xy_reload("tests/mods/nonexistent");
	assert(r == XY_ERR_NOTFOUND);
	printf("  test_reload_notfound: PASS\n");
}

/* -------------------------------------------------------------------------
 * Reload after full unload → XY_ERR_NOTFOUND
 * ------------------------------------------------------------------------- */

static void test_reload_after_unload(void) {
	assert(xy_load(MOD_PATH) == XY_OK);
	assert(xy_unload(MOD_PATH) == XY_OK);

	int r = xy_reload(MOD_PATH);
	assert(r == XY_ERR_NOTFOUND);

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
	assert(xy_load(MOD_PATH) == XY_OK);
	assert(xy_load(MOD_PATH2) == XY_OK);

	int v;
	XY_CALL(&v, get_counter, 0);
	assert(v == 99); /* mod_unload2 ran last */

	assert(xy_reload(MOD_PATH) == XY_OK);

	XY_CALL(&v, get_counter, 0);
	assert(v == 99); /* mod_unload2 still last */

	assert(xy_unload(MOD_PATH) == XY_OK);
	XY_CALL(&v, get_counter, 0);
	assert(v == 99); /* mod_unload2 alone */

	assert(xy_unload(MOD_PATH2) == XY_OK);
	v = 0;
	XY_CALL(&v, get_counter, 0);
	assert(v == 0);

	printf("  test_reload_position_preserved: PASS\n");
}

/* -------------------------------------------------------------------------
 * Cascade unload: unloading a parent also removes its child
 *
 * mod_cascade_parent loads mod_cascade_child during xy_install.
 * Because xy_install runs before the parent is appended to the dispatch
 * list, the order is: child (55) → parent (77).  Parent runs last → 77.
 * Unloading the parent must cascade-remove the child too.
 * ------------------------------------------------------------------------- */

static void test_cascade_unload(void) {
	assert(xy_load(MOD_CASCADE_P) == XY_OK);

	int v;
	XY_CALL(&v, get_counter, 0);
	assert(v == 77); /* parent ran last */

	assert(xy_unload(MOD_CASCADE_P) == XY_OK);

	v = 0;
	XY_CALL(&v, get_counter, 0);
	assert(v == 0); /* both parent and child gone */

	printf("  test_cascade_unload: PASS\n");
}

/* -------------------------------------------------------------------------
 * Cascade with shared child: child loaded independently is not cascade-removed
 *
 * Load the child directly (refcount=1, parent_entry=NULL), then load the
 * parent (parent's xy_install calls xy_load(child) again → duplicate path,
 * child refcount→2, parent_entry stays NULL).
 * The cascade filter is parent_entry==parent_entry; since the child's
 * parent_entry is NULL the cascade does not touch it.
 * After parent unload: child refcount stays 2, still active.
 * Two explicit unloads bring it to 0 and remove it.
 * ------------------------------------------------------------------------- */

static void test_cascade_shared_child(void) {
	assert(xy_load(MOD_CASCADE_C) == XY_OK);   /* child refcount=1, parent_entry=NULL */
	assert(xy_load(MOD_CASCADE_P) == XY_OK);   /* child refcount→2 (duplicate); parent appended after */

	int v;
	XY_CALL(&v, get_counter, 0);
	assert(v == 77); /* parent ran last */

	/* Unload parent — cascade does NOT touch the child (parent_entry==NULL).
	 * Parent is removed; child remains with refcount=2. */
	assert(xy_unload(MOD_CASCADE_P) == XY_OK);

	XY_CALL(&v, get_counter, 0);
	assert(v == 55); /* child still dispatching, now runs last */

	/* Two explicit unloads to drain the refcount from 2 to 0 */
	assert(xy_unload(MOD_CASCADE_C) == XY_OK); /* refcount 2→1, stays */
	XY_CALL(&v, get_counter, 0);
	assert(v == 55); /* still active */

	assert(xy_unload(MOD_CASCADE_C) == XY_OK); /* refcount 1→0, removed */
	v = 0;
	XY_CALL(&v, get_counter, 0);
	assert(v == 0);

	printf("  test_cascade_shared_child: PASS\n");
}

/* -------------------------------------------------------------------------
 * Unload from wrong region: a claim-promoted module is stored under its
 * child region key.  xy_unload from root builds a root-region key and
 * finds nothing → XY_ERR_NOTFOUND.  This is the correct / intended behaviour:
 * the caller must be in the right region context to unload.  In practice a
 * module in the same child region (e.g. a sibling loaded into that region)
 * would call xy_unload and it would succeed.  From the host (root) there is
 * no supported path to force-unload a claim-promoted module; xy_shutdown()
 * cleans everything up unconditionally.
 *
 * Setup: register a permissive claim handler at root, then load
 * mod_claim_simple (xy_claim=2).  Auto-claim fires; module is keyed under
 * the allocated child region, not root.
 * ------------------------------------------------------------------------- */

static int permissive_handler(const char *path, uint8_t req,
                               uint8_t *granted, void *ud) {
	(void)path; (void)ud;
	*granted = req;
	return XY_OK;
}

static void test_unload_wrong_region(void) {
	/* Register claim handler at root so mod_claim_simple auto-claims */
	int r = xy_require_claim(permissive_handler, NULL);
	assert(r == XY_OK);

	r = xy_load(MOD_CLAIM_SIMPLE);
	assert(r == XY_OK);

	/* Module is keyed under its child region, not root.
	 * Calling xy_unload from root must return XY_ERR_NOTFOUND. */
	r = xy_unload(MOD_CLAIM_SIMPLE);
	assert(r == XY_ERR_NOTFOUND);

	printf("  test_unload_wrong_region: PASS\n");
	/* xy_shutdown() in main cleans up */
}

/* -------------------------------------------------------------------------
 * Deep cascade unload: 3-level chain A → B → C
 *
 * Loading A triggers A's xy_install which loads B; B's xy_install loads C.
 * Install runs before append, so dispatch order is C→B→A (A appended last,
 * runs last, returns 11).
 * Unloading A must cascade through B (parent_entry=A) to C (parent_entry=B).
 * All three must be gone afterwards.
 * ------------------------------------------------------------------------- */

static void test_cascade_deep(void) {
	assert(xy_load(MOD_CASCADE_DEEP_A) == XY_OK);

	int v;
	XY_CALL(&v, get_counter, 0);
	assert(v == 11); /* A ran last */

	assert(xy_unload(MOD_CASCADE_DEEP_A) == XY_OK);

	v = 0;
	XY_CALL(&v, get_counter, 0);
	assert(v == 0); /* C, B, and A all gone */

	printf("  test_cascade_deep: PASS\n");
}

/* -------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */

int main(void) {
	printf("test_unload:\n");

	test_load_and_unload();
	xy_shutdown(); xy_init();

	test_refcount();
	xy_shutdown(); xy_init();

	test_unload_notfound();
	xy_shutdown(); xy_init();

	test_double_unload();
	xy_shutdown(); xy_init();

	test_reload();
	xy_shutdown(); xy_init();

	test_reload_notfound();
	xy_shutdown(); xy_init();

	test_reload_after_unload();
	xy_shutdown(); xy_init();

	test_reload_position_preserved();
	xy_shutdown(); xy_init();

	test_cascade_unload();
	xy_shutdown(); xy_init();

	test_cascade_shared_child();
	xy_shutdown(); xy_init();

	test_cascade_deep();
	xy_shutdown(); xy_init();

	test_unload_wrong_region();
	xy_shutdown();

	printf("  all tests passed\n");
	return 0;
}
