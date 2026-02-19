all := libndx libndx-mod
LDLIBS-libndx := -lqsys -lqmap

include ../mk/include.mk

TEST_DIR := tests
TEST_BIN := ${TEST_DIR}/test_main${EXE}
TEST_MOD := ${TEST_DIR}/test_mod.${SO}
TEST_CFLAGS := -Iinclude

${TEST_DIR}:
	@mkdir $@ 2>/dev/null || true

${TEST_BIN}: ${TEST_DIR} ${TEST_DIR}/test_main.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_main.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_MOD}: ${TEST_DIR} ${TEST_DIR}/test_mod.c lib/libndx-mod.${SO}
	${cc} -o $@ ${TEST_DIR}/test_mod.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx-mod

test: ${TEST_MOD} ${TEST_BIN}
	@LD_LIBRARY_PATH=./lib ./${TEST_BIN}
