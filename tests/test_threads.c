#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <ttypt/ndx.h>

static char mod_path[256];

NDX_DEF(int, thread_hook, int, val);

static void* load_thread(void *arg) {
	(void)arg;
	for (int i = 0; i < 50; i++) {
		int ret = ndx_load(mod_path);
		assert(ret == NDX_OK);
	}
	return NULL;
}

static void test_concurrent_load(void) {
	pthread_t threads[4];
	
	for (int i = 0; i < 4; i++) {
		pthread_create(&threads[i], NULL, load_thread, NULL);
	}
	for (int i = 0; i < 4; i++) {
		pthread_join(threads[i], NULL);
	}
	printf("  test_concurrent_load: PASS\n");
}

static void* call_thread(void *arg) {
	(void)arg;
	for (int i = 0; i < 100; i++) {
		int result = call_thread_hook(i);
		(void)result;
	}
	return NULL;
}

static void test_concurrent_call(void) {
	pthread_t threads[4];
	
	for (int i = 0; i < 4; i++) {
		pthread_create(&threads[i], NULL, call_thread, NULL);
	}
	for (int i = 0; i < 4; i++) {
		pthread_join(threads[i], NULL);
	}
	printf("  test_concurrent_call: PASS\n");
}

NDX_DEF(int, t1_hook, int, x);
NDX_DEF(int, t2_hook, int, x);
NDX_DEF(int, t3_hook, int, x);
NDX_DEF(int, t4_hook, int, x);

static void* areg_thread(void *arg) {
	int idx = *(int*)arg;
	switch (idx) {
		case 0: t1_hook_adapter_reg(); break;
		case 1: t2_hook_adapter_reg(); break;
		case 2: t3_hook_adapter_reg(); break;
		case 3: t4_hook_adapter_reg(); break;
	}
	return NULL;
}

static void test_concurrent_areg(void) {
	pthread_t threads[4];
	int indices[4] = {0, 1, 2, 3};
	
	for (int i = 0; i < 4; i++) {
		pthread_create(&threads[i], NULL, areg_thread, &indices[i]);
	}
	for (int i = 0; i < 4; i++) {
		pthread_join(threads[i], NULL);
	}
	
	assert(t1_hook_adapter.name[0] != '\0');
	assert(t2_hook_adapter.name[0] != '\0');
	assert(t3_hook_adapter.name[0] != '\0');
	assert(t4_hook_adapter.name[0] != '\0');
	printf("  test_concurrent_areg: PASS\n");
}

static void* get_thread(void *arg) {
	(void)arg;
	for (int i = 0; i < 100; i++) {
		assert(thread_hook_adapter.name[0] != '\0');
	}
	return NULL;
}

static void test_concurrent_get(void) {
	pthread_t threads[8];
	
	for (int i = 0; i < 8; i++) {
		pthread_create(&threads[i], NULL, get_thread, NULL);
	}
	for (int i = 0; i < 8; i++) {
		pthread_join(threads[i], NULL);
	}
	printf("  test_concurrent_get: PASS\n");
}

static void build_mod_path(void) {
	snprintf(mod_path, sizeof(mod_path), "./tests/mods/mod_basic");
}

int main(void) {
	printf("test_threads:\n");
	
	build_mod_path();
	
	int ret = ndx_load(mod_path);
	assert(ret == NDX_OK);
	
	test_concurrent_load();
	test_concurrent_call();
	test_concurrent_areg();
	test_concurrent_get();
	
	printf("  all tests passed\n");
	return 0;
}
