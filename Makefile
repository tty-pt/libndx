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

${TEST_DIR}/mods/mod_pledge_owner.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_pledge_owner.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_pledge_owner.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_pledge_interloper.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_pledge_interloper.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_pledge_interloper.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_region_worker.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_region_worker.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_region_worker.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_region_moderator.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_region_moderator.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_region_moderator.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_claim_god.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_claim_god.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_claim_god.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_claim_worker.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_claim_worker.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_claim_worker.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_claim_greedy.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_claim_greedy.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_claim_greedy.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_unload.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_unload.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_unload.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_unload2.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_unload2.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_unload2.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_cascade_child.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_cascade_child.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_cascade_child.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_cascade_parent.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_cascade_parent.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_cascade_parent.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_claim_simple.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_claim_simple.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_claim_simple.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_cascade_deep_a.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_cascade_deep_a.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_cascade_deep_a.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_cascade_deep_b.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_cascade_deep_b.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_cascade_deep_b.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_cascade_deep_c.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_cascade_deep_c.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_cascade_deep_c.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_region_state.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_region_state.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_region_state.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_ptr_args.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_ptr_args.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_ptr_args.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_ptr_args_caller.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_ptr_args_caller.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_ptr_args_caller.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/test_ptr_args${EXE}: ${TEST_DIR} ${TEST_DIR}/test_ptr_args.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_ptr_args.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/bench_dispatch${EXE}: ${TEST_DIR} ${TEST_DIR}/bench_dispatch.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/bench_dispatch.c ${CFLAGS} ${TEST_CFLAGS} -O2 \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_region_state${EXE}: ${TEST_DIR} ${TEST_DIR}/test_region_state.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_region_state.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_unload${EXE}: ${TEST_DIR} ${TEST_DIR}/test_unload.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_unload.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_pledge${EXE}: ${TEST_DIR} ${TEST_DIR}/test_pledge.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_pledge.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_threads${EXE}: ${TEST_DIR} ${TEST_DIR}/test_threads.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_threads.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_region${EXE}: ${TEST_DIR} ${TEST_DIR}/test_region.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_region.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

${TEST_DIR}/test_fn_hook${EXE}: ${TEST_DIR} ${TEST_DIR}/test_fn_hook.c ${TEST_DIR}/fn_hook_hooks.h lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_fn_hook.c ${CFLAGS} ${TEST_CFLAGS} \
		-I${TEST_DIR} ${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

GAME_HOOKS := ${TEST_DIR}/game_hooks.h

${TEST_DIR}/mods/mod_game_world.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_game_world.c ${GAME_HOOKS} lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_game_world.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_game_physics.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_game_physics.c ${GAME_HOOKS} lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_game_physics.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_game_combat.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_game_combat.c ${GAME_HOOKS} lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_game_combat.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_game_loot.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_game_loot.c ${GAME_HOOKS} lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_game_loot.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_game_ai.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_game_ai.c ${GAME_HOOKS} lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_game_ai.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/mods/mod_game_no_claim.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_game_no_claim.c lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_game_no_claim.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lndx ${LDLIBS-libndx}

${TEST_DIR}/test_game${EXE}: ${TEST_DIR} ${TEST_DIR}/test_game.c ${GAME_HOOKS} lib/libndx.${SO}
	${cc} -o $@ ${TEST_DIR}/test_game.c ${CFLAGS} ${TEST_CFLAGS} \
		-I${TEST_DIR} ${LDFLAGS} -lndx ${LDLIBS-libndx} ${TEST_LDFLAGS}

TEST_MODS := ${TEST_DIR}/test_mod.${SO} \
	${TEST_DIR}/mods/mod_basic.${SO} \
	${TEST_DIR}/mods/mod_multi.${SO} \
	${TEST_DIR}/mods/mod_void.${SO} \
	${TEST_DIR}/mods/mod_bare.${SO} \
	${TEST_DIR}/mods/mod_dep_provider.${SO} \
	${TEST_DIR}/mods/mod_dep_consumer.${SO} \
	${TEST_DIR}/mods/mod_auto.${SO} \
	${TEST_DIR}/mods/mod_adder.${SO} \
	${TEST_DIR}/mods/mod_multiplier.${SO} \
	${TEST_DIR}/mods/mod_pledge_owner.${SO} \
	${TEST_DIR}/mods/mod_pledge_interloper.${SO} \
	${TEST_DIR}/mods/mod_region_worker.${SO} \
	${TEST_DIR}/mods/mod_region_moderator.${SO} \
	${TEST_DIR}/mods/mod_claim_god.${SO} \
	${TEST_DIR}/mods/mod_claim_worker.${SO} \
	${TEST_DIR}/mods/mod_claim_greedy.${SO} \
	${TEST_DIR}/mods/mod_game_world.${SO} \
	${TEST_DIR}/mods/mod_game_physics.${SO} \
	${TEST_DIR}/mods/mod_game_combat.${SO} \
	${TEST_DIR}/mods/mod_game_loot.${SO} \
	${TEST_DIR}/mods/mod_game_ai.${SO} \
	${TEST_DIR}/mods/mod_game_no_claim.${SO} \
	${TEST_DIR}/mods/mod_unload.${SO} \
	${TEST_DIR}/mods/mod_unload2.${SO} \
	${TEST_DIR}/mods/mod_cascade_child.${SO} \
	${TEST_DIR}/mods/mod_cascade_parent.${SO} \
	${TEST_DIR}/mods/mod_claim_simple.${SO} \
	${TEST_DIR}/mods/mod_cascade_deep_a.${SO} \
	${TEST_DIR}/mods/mod_cascade_deep_b.${SO} \
	${TEST_DIR}/mods/mod_cascade_deep_c.${SO} \
	${TEST_DIR}/mods/mod_region_state.${SO} \
	${TEST_DIR}/mods/mod_ptr_args.${SO} \
	${TEST_DIR}/mods/mod_ptr_args_caller.${SO}

TEST_BINS := ${TEST_DIR}/test_core${EXE} \
	${TEST_DIR}/test_errors${EXE} \
	${TEST_DIR}/test_macros${EXE} \
	${TEST_DIR}/test_main${EXE} \
	${TEST_DIR}/test_deps${EXE} \
	${TEST_DIR}/test_auto_init${EXE} \
	${TEST_DIR}/test_multi_call${EXE} \
	${TEST_DIR}/test_get${EXE} \
	${TEST_DIR}/test_pledge${EXE} \
	${TEST_DIR}/test_region${EXE} \
	${TEST_DIR}/test_fn_hook${EXE} \
	${TEST_DIR}/test_game${EXE} \
	${TEST_DIR}/test_unload${EXE} \
	${TEST_DIR}/test_region_state${EXE} \
	${TEST_DIR}/test_ptr_args${EXE}

test-build: lib/libndx.${SO} ${TEST_MODS} ${TEST_BINS}

test: test-build
	@echo "Running test_core..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_core
	@echo "Running test_errors..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_errors
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
	@echo "Running test_pledge..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_pledge
	@echo "Running test_region..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_region
	@echo "Running test_fn_hook..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_fn_hook
	@echo "Running test_game..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_game
	@echo "Running test_unload..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_unload
	@echo "Running test_region_state..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_region_state
	@echo "Running test_ptr_args..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_ptr_args
	@echo "All tests passed!"

bench: test-build ${TEST_DIR}/bench_dispatch${EXE}
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/bench_dispatch
