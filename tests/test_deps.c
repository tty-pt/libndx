#include <assert.h>
#include <stdio.h>
#include <ttypt/xy.h>
#include "test_hooks.h"

static void test_load_provider_first(void) {
    int ret = xy_load("./tests/mods/mod_dep_provider");
    assert(ret == XY_OK);
    
    int count = get_counter(0);
    assert(count == 100);
    printf("  test_load_provider_first: PASS\n");
}

static void test_load_consumer_loads_provider(void) {
    int count_before = get_counter(0);
    assert(count_before == 100);
    
    int ret = xy_load("./tests/mods/mod_dep_consumer");
    assert(ret == XY_OK);
    
    int count_after = get_counter(0);
    assert(count_after == 100);
    printf("  test_load_consumer_loads_provider: PASS\n");
}

static void test_increment_via_consumer(void) {
    increment_counter(5);
    
    int count = get_counter(0);
    assert(count == 105);
    printf("  test_increment_via_consumer: PASS\n");
}

int main(void) {
    printf("test_deps:\n");
    
    test_load_provider_first();
    test_load_consumer_loads_provider();
    test_increment_via_consumer();
    
    printf("  all tests passed\n");
    return 0;
}
