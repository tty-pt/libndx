#include <assert.h>
#include <stdio.h>
#include <ttypt/xy.h>

XY_HOOK_DEF(int, add_value, int, x);

static void test_multi_call_runs_all_modules(void) {
    int ret = xy_load("./tests/mods/mod_adder");
    assert(ret == XY_OK);
    
    ret = xy_load("./tests/mods/mod_multiplier");
    assert(ret == XY_OK);
    
    int result = add_value(5);
    
    printf("  test_multi_call_runs_all_modules: result=%d (should be last module: 10)\n", result);
    assert(result == 10);
    printf("  test_multi_call_runs_all_modules: PASS\n");
}

int main(void) {
    printf("test_multi_call:\n");
    
    test_multi_call_runs_all_modules();
    
    printf("  all tests passed\n");
    return 0;
}
