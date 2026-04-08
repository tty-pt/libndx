#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/ndx.h>

NDX_DEF(int, foo_hook, int, x);
NDX_DEF(int, bar_hook, int, x);

static void test_adapter_registered(void) {
    assert(strcmp(foo_hook_adapter.name, "foo_hook") == 0);
    assert(strcmp(bar_hook_adapter.name, "bar_hook") == 0);
    printf("  test_adapter_registered: PASS\n");
}

static void test_adapter_sizes(void) {
    assert(foo_hook_adapter.arg_size == sizeof(struct foo_hook_args));
    assert(foo_hook_adapter.ret_size == sizeof(int));
    printf("  test_adapter_sizes: PASS\n");
}

int main(void) {
    printf("test_get:\n");

    test_adapter_registered();
    test_adapter_sizes();

    printf("  all tests passed\n");
    return 0;
}
