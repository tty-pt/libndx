#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/ndx.h>

NDX_DECL(int, foo_hook, int, x);
NDX_DEF(int, foo_hook, int, x);
NDX_DECL(int, bar_hook, int, x);
NDX_DEF(int, bar_hook, int, x);

static void test_get_valid(void) {
    fprintf(stderr, "  before ndx_get: foo_hook_id = %u\n", foo_hook_id);
    unsigned id = ndx_get("foo_hook");
    fprintf(stderr, "  ndx_get returned %u, foo_hook_id = %u\n", id, foo_hook_id);
    fflush(stderr);
    assert(id == foo_hook_id);
    assert(id != NDX_INVALID);
    printf("  test_get_valid: PASS\n");
}

static void test_get_invalid(void) {
    unsigned id = ndx_get("nonexistent_hook");
    assert(id == NDX_INVALID);
    printf("  test_get_invalid: PASS\n");
}

static void test_get_after_load(void) {
    unsigned id = ndx_get("foo_hook");
    assert(id == foo_hook_id);
    assert(id != NDX_INVALID);
    printf("  test_get_after_load: PASS\n");
}

int main(void) {
    printf("test_get:\n");
    
    test_get_valid();
    test_get_invalid();
    test_get_after_load();
    
    printf("  all tests passed\n");
    return 0;
}
