#include "libndx-internal.h"

int ndx_last(void *ret) {
	int retc = ndx_runtime_ensure();
	if (retc != NDX_OK) {
		NDX_SET_ERR(retc);
		return retc;
	}
	if (!ndx.adapter) {
		NDX_SET_ERR(NDX_ERR_INVALID);
		return NDX_ERR_INVALID;
	}
	/* T1.5: ran stored in TLS; read ret bytes from TLS retp pointer */
	if (!ndx_last_ran) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}
	if (ret && ndx_last_retp) {
		memcpy(ret, ndx_last_retp, ndx.adapter->ret_size);
	}
	NDX_SET_ERR(NDX_OK);
	return NDX_OK;
}

/* -------------------------------------------------------------------------
 * ndx_call — region-aware dispatch
 * ------------------------------------------------------------------------- */

/*
 * fn_cache_resolve — lazy per-module, per-hook dlsym cache.
 *
 * DISPATCH-SAFETY INVARIANT: this helper is called from inside the DFS
 * dispatch loop of ndx_call. It MUST NOT read ndx_last_adapter.* — that TLS
 * slot is overwritten whenever a module body nested-calls another hook via
 * ndx_call, and we are mid-iteration of the *outer* call.
 *
 * Caller must supply hook_id and name from a stable source on its stack.
 *
 * Returns resolved callback, or NULL if the module does not export this
 * hook. Returns (void *)-1 (cast via NDX_FN_RESOLVE_OOM) if cache growth
 * fails; caller treats this as abort (returns NDX_ERR_INVALID).
 */
#define NDX_FN_RESOLVE_OOM ((void *)(uintptr_t)-1)

static inline void * __attribute__((always_inline, hot))
fn_cache_resolve(ndx_mod_entry_t *me, int hook_id, const char *name)
{
	if (likely(hook_id >= 0 && hook_id < me->fn_cache_cap)) {
		void *cb = me->fn_cache[hook_id];
		if (likely(cb)) return cb == NDX_FN_NOT_FOUND ? NULL : cb;
	} else if (hook_id >= 0) {
		if (module_ensure_fn_cache_cap(me, hook_id + 16) < 0)
			return NDX_FN_RESOLVE_OOM;
	}

	void *cb = module_lookup_symbol_raw(me->handle, name);
	if (hook_id >= 0)
		me->fn_cache[hook_id] = cb ? cb : NDX_FN_NOT_FOUND;
	if (cb && hook_id >= 0)
		module_mark_hook_implemented(me, hook_id);
	return cb;
}

/*
 * fn_cache_prewarm — eagerly resolve all currently-known hooks for one
 * module. Called at module-load time (T1.1) so the first hot-path call
 * doesn't pay dlsym latency. Also called from ndx_areg for every loaded
 * module when a new hook ID is minted, so the cache stays warm as the
 * hook ID space grows.
 *
 * Returns 0 on success, -1 if cache growth (realloc) failed; caller
 * should treat -1 as a soft failure (lazy resolve will retry per-call).
 */
int
fn_cache_prewarm(ndx_mod_entry_t *me)
{
	if (!me || !me->handle) return 0;
	int needed = ndx_hook_id_counter;
	if (needed <= 0) return 0;
	if (module_ensure_fn_cache_cap(me, needed + 16) < 0) return -1;
	/* Iterate hook_id_hd (name → hook_id) and dlsym each in this module */
	unsigned c = qmap_iter(hook_id_hd, NULL, 0);
	const void *key, *value;
	while (qmap_next(&key, &value, c)) {
		const char *name = key;
		int hook_id = *(const int *)value;
		if (hook_id < 0 || hook_id >= me->fn_cache_cap) continue;
		if (me->fn_cache[hook_id]) continue; /* already resolved */
		void *cb = module_lookup_symbol_raw(me->handle, name);
		me->fn_cache[hook_id] = cb ? cb : NDX_FN_NOT_FOUND;
		if (cb)
			module_mark_hook_implemented(me, hook_id);
	}
	return 0;
}

/*
 * ndx_dispatch_module — single per-module dispatch step used by ndx_call.
 *
 * always_inline keeps this wrapper out of the hot-path call graph.
 *
 * Returns:
 *   0                  → ran successfully (caller bumps ran counter)
 *  -1                  → skipped (module doesn't export this hook, or
 *                        me->indx is NULL)
 *   NDX_ERR_INVALID    → fn_cache growth failed; caller propagates per
 *                        its dispatch context (fast path: set errno +
 *                        return
 *
 * DISPATCH-SAFETY INVARIANT: helper takes hook_id/name/adapter as
 * parameters from caller's stack. It does NOT read ndx_last_adapter.
 * See fn_cache_resolve comment.
 */
int __attribute__((hot))
ndx_dispatch_module(ndx_mod_entry_t *me,
                    void            *cb,
	ndx_adapter_t   *adapter,
	void           (*call_fn)(void *, void *, void *),
	void            *retp,
	void            *arg,
	uint64_t         current_region_id)
{
	ndx_t *indx = me->indx;
	if (unlikely(!indx)) return -1;
	if (unlikely(!cb)) return -1;

	indx->adapter      = adapter;
	indx->region_state = me->region_state;

	ndx_region_entry_t *prev_rentry = ndx_current_region_entry;
	uint64_t region_changed = (indx->region_id != current_region_id);
	if (unlikely(region_changed))
		set_current_region(indx->region_id, me->region_entry);

	call_fn(retp, cb, arg);

	if (unlikely(region_changed))
		set_current_region(current_region_id, prev_rentry);

	return 0;
}

typedef struct {
	int                 hook_id;
	void              (*dispatch_call)(void *, void *, void *);
	ndx_region_entry_t *caller_region_entry;
	int                 has_denies;
	ndx_region_entry_t *anc_chain[65];
	int                 anc_n;
} ndx_call_plan_t;

typedef struct {
	void               *retp;
	ndx_adapter_t      *reg;
	void               *arg;
	uint64_t            region_id;
	int                 pre_err;
	int                 ret;
	unsigned            ran;
	ndx_call_plan_t     plan;
} ndx_call_ctx_t;

static int
ndx_call_run_fast(ndx_call_ctx_t *ctx);

static int
ndx_call_resolve_region(ndx_call_ctx_t *ctx)
{
	ndx_region_entry_t *caller_region_entry = ndx_current_region_entry;
	if (unlikely(!caller_region_entry))
		caller_region_entry = region_lookup(ctx->region_id);

	uint8_t sflags = caller_region_entry ? caller_region_entry->subtree_flags : 0;
	ctx->plan.caller_region_entry = caller_region_entry;
	ctx->plan.has_denies = (sflags & NDX_SUBTREE_SECURITY_MASK) != 0;
	ctx->plan.anc_n = 0;

	if (unlikely(sflags & NDX_SUBTREE_ANY_MASK) && caller_region_entry)
		ctx->plan.anc_n = region_ancestor_chain(caller_region_entry, ctx->plan.anc_chain, 65);

	return NDX_OK;
}

static int
ndx_call_prepare(ndx_call_ctx_t *ctx)
{
	ndx_adapter_t *reg = ctx->reg;
	int hook_id = reg->hook_id;

	if (unlikely(hook_id < 0)) {
		const void *hid_v = qmap_get(hook_id_hd, reg->name);
		if (!hid_v)
			return NDX_ERR_NOTFOUND;
		hook_id = *(const int *)hid_v;
		reg->hook_id = hook_id;
		if (hook_id >= 0 && hook_id < ndx_adapter_by_id_cap) {
			const ndx_adapter_t *canonical = ndx_adapter_by_id[hook_id];
			if (canonical && !reg->call) {
				reg->call     = canonical->call;
				reg->ret_size = canonical->ret_size;
				reg->arg_size = canonical->arg_size;
			}
		}
		if (unlikely(reg->ret_size > NDX_MAX_RET_SIZE))
			return NDX_ERR_TOOBIG;
	}

	void (*dispatch_call)(void *, void *, void *) = reg->call;
	if (unlikely(!dispatch_call && hook_id >= 0 && hook_id < ndx_adapter_by_id_cap)) {
		const ndx_adapter_t *canonical = ndx_adapter_by_id[hook_id];
		if (canonical)
			dispatch_call = canonical->call;
	}
	if (unlikely(!dispatch_call))
		return NDX_ERR_NOTFOUND;

	ctx->plan.hook_id = hook_id;
	ctx->plan.dispatch_call = dispatch_call;
	return NDX_OK;
}

static int
ndx_call_check_region_security(ndx_call_ctx_t *ctx)
{
	ndx_adapter_t *reg = ctx->reg;

	if (likely(!ctx->plan.has_denies))
		return NDX_OK;

	for (int i = 0; i < ctx->plan.anc_n; i++) {
		if (ctx->plan.anc_chain[i]->denied_hooks_set &&
		    qmap_get(ctx->plan.anc_chain[i]->denied_hooks_set, reg->name))
			return NDX_ERR_EPERM;
	}

	return NDX_OK;
}

static int
ndx_call_check_security(ndx_call_ctx_t *ctx)
{
	int ret = ndx_call_resolve_region(ctx);
	if (ret != NDX_OK)
		return ret;
	return ndx_call_check_region_security(ctx);
}

static inline void
ndx_call_prime_last(ndx_adapter_t *adapter, void *retp)
{
	ndx_last_ran = 0;
	ndx_last_retp = retp;
	ndx.adapter = adapter;
}

static inline void
ndx_call_publish_ret(ndx_call_ctx_t *ctx, const void *ret_src)
{
	ndx_last_ran = ctx->ran;
	ndx_set_last_ret(ctx->ran ? ret_src : NULL, ctx->reg->ret_size);
	if (!ctx->ran && ctx->retp)
		memset(ctx->retp, 0, ctx->reg->ret_size);
	ndx.adapter = ctx->reg;
}

static inline void
ndx_call_publish_err(ndx_call_ctx_t *ctx)
{
	ndx_last_ran = 0;
	ndx_set_last_ret(NULL, ctx->reg ? ctx->reg->ret_size : 0);
	ndx_zero_ret(ctx->retp, ctx->reg);
	ndx.adapter = ctx->reg;
}

static int
region_ensure_hook_dispatch_cap(ndx_region_entry_t *re, int hook_id)
{
	if (!re || hook_id < 0)
		return NDX_ERR_INVALID;
	if (hook_id < re->hook_dispatch_cap)
		return NDX_OK;

	int new_cap = hook_id + 16;
	ndx_hook_dispatch_cache_t *tmp = realloc(re->hook_dispatch,
		new_cap * sizeof(*tmp));
	if (unlikely(!tmp))
		return NDX_ERR_INVALID;

	for (int i = re->hook_dispatch_cap; i < new_cap; i++) {
		tmp[i].slots = NULL;
		tmp[i].count = 0;
		tmp[i].cap = 0;
		tmp[i].region_gen = 0;
	}

	re->hook_dispatch = tmp;
	re->hook_dispatch_cap = new_cap;
	return NDX_OK;
}

/* Build the region-local dispatch vector for one hook by filtering the
 * subtree module list down to actual listeners that survive deny checks. */
static int
region_rebuild_hook_dispatch(ndx_region_entry_t *re, int hook_id,
                             const char *hook_name,
                             ndx_region_entry_t **anc_chain, int anc_n)
{
	if (!re || hook_id < 0 || !hook_name)
		return NDX_ERR_INVALID;
	if (re->subtree_mods_dirty && region_rebuild_subtree_mods(re) < 0)
		return NDX_ERR_INVALID;
	if (region_ensure_hook_dispatch_cap(re, hook_id) != NDX_OK)
		return NDX_ERR_INVALID;

	ndx_hook_dispatch_cache_t *cache = &re->hook_dispatch[hook_id];
	if (cache->region_gen == re->dispatch_gen)
		return NDX_OK;
	cache->count = 0;

	for (int mi = 0; mi < re->subtree_mods_count; mi++) {
		ndx_mod_entry_t *me = re->subtree_mods[mi];
		if (!me || !me->indx)
			continue;
		if (anc_n > 0 && module_denied_by_ancestors(me, anc_chain, anc_n))
			continue;

		if (!module_has_hook_implemented(me, hook_id)) {
			void *cb = fn_cache_resolve(me, hook_id, hook_name);
			if (unlikely(cb == NDX_FN_RESOLVE_OOM))
				return NDX_ERR_INVALID;
			if (!cb)
				continue;
		}
		if (!module_has_hook_implemented(me, hook_id))
			continue;

		if (cache->count >= cache->cap) {
			int new_cap = cache->cap ? cache->cap * 2 : 8;
			ndx_dispatch_slot_t *tmp = realloc(cache->slots,
				new_cap * sizeof(*tmp));
			if (unlikely(!tmp))
				return NDX_ERR_INVALID;
			cache->slots = tmp;
			cache->cap = new_cap;
		}
		cache->slots[cache->count].me = me;
		cache->slots[cache->count].cb = me->fn_cache[hook_id];
		cache->count++;
	}

	cache->region_gen = re->dispatch_gen;
	return NDX_OK;
}

static int
ndx_call_run_fast(ndx_call_ctx_t *ctx)
{
	ctx->ran = 0;
	ndx_call_prime_last(ctx->reg, ctx->retp);

	ndx_region_entry_t *re = ctx->plan.caller_region_entry;
	if (!re)
		return NDX_OK;
	if (unlikely(re->subtree_mods_dirty) && region_rebuild_subtree_mods(re) < 0)
		return NDX_ERR_INVALID;
	if (unlikely(ctx->plan.hook_id >= re->hook_dispatch_cap) &&
	    region_ensure_hook_dispatch_cap(re, ctx->plan.hook_id) != NDX_OK)
		return NDX_ERR_INVALID;

	ndx_hook_dispatch_cache_t *cache =
		&re->hook_dispatch[ctx->plan.hook_id];
	if (unlikely(cache->region_gen != re->dispatch_gen) &&
	    region_rebuild_hook_dispatch(re,
	                                 ctx->plan.hook_id,
	                                 ctx->reg->name,
	                                 ctx->plan.anc_chain,
	                                 ctx->plan.anc_n) != NDX_OK)
		return NDX_ERR_INVALID;
	ndx_dispatch_slot_t *slots = cache->slots;
	int n = cache->count;
	for (int mi = 0; mi < n; mi++) {
		ndx_mod_entry_t *me = slots[mi].me;
		if (mi + 1 < n)
			__builtin_prefetch(&slots[mi + 1], 0, 1);
		int did_run = dispatch_fast_module(me, slots[mi].cb,
		                                   ctx->reg, ctx->plan.dispatch_call,
		                                   ctx->retp, ctx->arg,
		                                   ctx->region_id);
		if (unlikely(did_run == NDX_ERR_INVALID))
			return NDX_ERR_INVALID;
		ctx->ran += did_run;
	}

	return NDX_OK;
}

static int
ndx_call_fail(ndx_call_ctx_t *ctx)
{
	ndx_call_publish_err(ctx);
	NDX_SET_ERR(ctx->ret);
	return ctx->ret;
}

int __attribute__((hot, flatten))
ndx_call(void *retp, ndx_adapter_t *reg, void *arg)
{
	if (unlikely(!ndx_inited)) {
		int ret = ndx_runtime_ensure();
		if (ret != NDX_OK) {
			ndx_zero_ret(retp, reg);
			NDX_SET_ERR(ret);
			return ret;
		}
	}

	if (!reg) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

	ndx_call_ctx_t ctx = {
		.retp = retp,
		.reg = reg,
		.arg = arg,
		.region_id = ndx_current_region_id,
		.pre_err = NDX_GET_ERR(),
		.ret = NDX_OK,
	};

	ctx.ret = ndx_call_prepare(&ctx);
	if (ctx.ret != NDX_OK)
		return ndx_call_fail(&ctx);

	ctx.ret = ndx_call_check_security(&ctx);
	if (ctx.ret != NDX_OK)
		return ndx_call_fail(&ctx);

	ctx.ret = ndx_call_run_fast(&ctx);
	if (ctx.ret != NDX_OK)
		return ndx_call_fail(&ctx);

	ndx_call_publish_ret(&ctx, ctx.retp);
	if (ctx.pre_err != NDX_OK && NDX_GET_ERR() == ctx.pre_err)
		NDX_SET_ERR(NDX_OK);
	return NDX_OK;
}

/* -------------------------------------------------------------------------
 * ndx_areg
 * ------------------------------------------------------------------------- */

unsigned
ndx_areg(char *name, ndx_adapter_t *adapter)
{
	int enter_ret = ndx_runtime_ensure();
	if (enter_ret != NDX_OK) {
		NDX_SET_ERR(enter_ret);
		return NDX_INVALID;
	}
	if (adapter->ret_size > NDX_MAX_RET_SIZE) {
		NDX_SET_ERR(NDX_ERR_TOOBIG);
		return NDX_INVALID;
	}
	const unsigned *existing = qmap_get(sica_hd, name);
	if (existing) {
		unsigned id = *existing;
	    return id;
	}
	qmap_put(sica_hd, name, &adapter);
	/* Assign a monotonic hook ID for the fn_cache index */
	int hook_id = ndx_hook_id_counter++;
	qmap_put(hook_id_hd, name, &hook_id);
	/* Write the ID back into the adapter so callers can skip the hash lookup */
	adapter->hook_id = hook_id;
	/* Grow adapter-by-id array and store pointer */
	if (hook_id >= ndx_adapter_by_id_cap) {
		int new_cap = hook_id + 16;
		ndx_adapter_t **tmp = realloc(ndx_adapter_by_id,
		                              new_cap * sizeof(ndx_adapter_t *));
		if (unlikely(!tmp)) {
			NDX_SET_ERR(NDX_ERR_INVALID);
			return NDX_ERR_INVALID;
		}
		ndx_adapter_by_id = tmp;
		memset(ndx_adapter_by_id + ndx_adapter_by_id_cap, 0,
		       (new_cap - ndx_adapter_by_id_cap) * sizeof(ndx_adapter_t *));
		ndx_adapter_by_id_cap = new_cap;
	}
	ndx_adapter_by_id[hook_id] = adapter;
	/* T1.1: a new hook ID has been minted — eagerly resolve it in every
	 * already-loaded module so the first dispatch finds it cached. */
	if (mod_hd) {
		unsigned c = qmap_iter(mod_hd, NULL, 0);
		const void *k, *v;
		while (qmap_next(&k, &v, c)) {
			ndx_mod_entry_t *m = qmap_ptr(v);
			if (m) (void)fn_cache_prewarm(m);
		}
	}
	NDX_SET_ERR(NDX_OK);
	return 0;
}
