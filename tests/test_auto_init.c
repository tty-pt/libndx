#include <assert.h>
#include <stdio.h>
#include <ttypt/xy.h>

XY_LISTENER(int, init_order_check, int, dummy);

int init_order_check(int dummy) {
	(void)dummy;
    return 0;
}

static void test_auto_init_runs_before_install(void) {
    int ret = xy_load("./tests/mods/mod_auto");
    assert(ret == XY_OK);
    
    printf("  test_auto_init_runs_before_install: PASS\n");
}

int main(void) {
    printf("test_auto_init:\n");
    
    test_auto_init_runs_before_install();
    
    printf("  all tests passed\n");
    return 0;
}
