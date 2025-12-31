all := libndx
LDLIBS-libndx := -lqsys -lqmap

include ../mk/include.mk

TEST_DIR := tests
TEST_CFLAGS := -Iinclude -pthread
TEST_LDFLAGS := -pthread

${TEST_DIR}:
	@mkdir -p ${TEST_DIR}/mods 2>/dev/null || true

${TEST_DIR}/test_core${EXE}: ${TEST_DIR} ${TEST_DIR}/test_core.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_core.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_errors${EXE}: ${TEST_DIR} ${TEST_DIR}/test_errors.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_errors.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_threads${EXE}: ${TEST_DIR} ${TEST_DIR}/test_threads.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_threads.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_macros${EXE}: ${TEST_DIR} ${TEST_DIR}/test_macros.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_macros.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_main${EXE}: ${TEST_DIR} ${TEST_DIR}/test_main.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_main.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/test_deps${EXE}: ${TEST_DIR} ${TEST_DIR}/test_deps.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_deps.c ${CFLAGS} ${TEST_CFLAGS} -Itests \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_auto_init${EXE}: ${TEST_DIR} ${TEST_DIR}/test_auto_init.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_auto_init.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_multi_call${EXE}: ${TEST_DIR} ${TEST_DIR}/test_multi_call.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_multi_call.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_get${EXE}: ${TEST_DIR} ${TEST_DIR}/test_get.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_get.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_mod.${SO}: ${TEST_DIR} ${TEST_DIR}/test_mod.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_mod.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_basic.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_basic.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_basic.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_multi.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_multi.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_multi.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_void.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_void.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_void.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_bare.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_bare.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_bare.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_dep_provider.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_dep_provider.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_dep_provider.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_dep_consumer.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_dep_consumer.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_dep_consumer.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_auto.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_auto.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_auto.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_adder.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_adder.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_adder.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_multiplier.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_multiplier.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_multiplier.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

TEST_MODS := ${TEST_DIR}/test_mod.${SO} \
	${TEST_DIR}/mods/mod_basic.${SO} \
	${TEST_DIR}/mods/mod_multi.${SO} \
	${TEST_DIR}/mods/mod_void.${SO} \
	${TEST_DIR}/mods/mod_bare.${SO} \
	${TEST_DIR}/mods/mod_dep_provider.${SO} \
	${TEST_DIR}/mods/mod_dep_consumer.${SO} \
	${TEST_DIR}/mods/mod_auto.${SO} \
	${TEST_DIR}/mods/mod_adder.${SO} \
	${TEST_DIR}/mods/mod_multiplier.${SO}

TEST_BINS := ${TEST_DIR}/test_core${EXE} \
	${TEST_DIR}/test_errors${EXE} \
	${TEST_DIR}/test_threads${EXE} \
	${TEST_DIR}/test_macros${EXE} \
	${TEST_DIR}/test_main${EXE} \
	${TEST_DIR}/test_deps${EXE} \
	${TEST_DIR}/test_auto_init${EXE} \
	${TEST_DIR}/test_multi_call${EXE} \
	${TEST_DIR}/test_get${EXE}

test-build: lib/libndx.${SO} ${TEST_MODS} ${TEST_BINS}

test: test-build
	@echo "Running test_core..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_core
	@echo "Running test_errors..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_errors
	@echo "Running test_threads..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_threads
	@echo "Running test_macros..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_macros
	@echo "Running test_main..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_main
	@echo "Running test_deps..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_deps
	@echo "Running test_auto_init..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_auto_init
	@echo "Running test_multi_call..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_multi_call
	@echo "Running test_get..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_get
	@echo "All tests passed!"
