#include <assert.h>
#include <stdio.h>
#include <ttypt/ndx.h>

NDX_DEF(int, get_counter, int, dummy) {
	(void)dummy;
	return 0;
}
NDX_DEF(int, increment_counter, int, amount) {
	(void)amount;
	return 0;
}

static void test_load_provider_first(void) {
    int ret = ndx_load("./tests/mods/mod_dep_provider");
    assert(ret == NDX_OK);
    
    int count = call_get_counter(0);
    assert(count == 100);
    printf("  test_load_provider_first: PASS\n");
}

static void test_load_consumer_loads_provider(void) {
    int count_before = call_get_counter(0);
    assert(count_before == 100);
    
    int ret = ndx_load("./tests/mods/mod_dep_consumer");
    assert(ret == NDX_OK);
    
    int count_after = call_get_counter(0);
    assert(count_after == 100);
    printf("  test_load_consumer_loads_provider: PASS\n");
}

static void test_increment_via_consumer(void) {
    call_increment_counter(5);
    
    int count = call_get_counter(0);
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
