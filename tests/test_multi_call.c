#include <assert.h>
#include <stdio.h>
#include <ttypt/ndx.h>

NDX_DEF(int, add_value, int, x);

static void test_multi_call_runs_all_modules(void) {
    int ret = ndx_load("./tests/mods/mod_adder");
    assert(ret == NDX_OK);
    
    ret = ndx_load("./tests/mods/mod_multiplier");
    assert(ret == NDX_OK);
    
    int result = call_add_value(5);
    
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
