#include "libxylem-internal.h"

int xy_last(void *ret) {
	int retc = xy_runtime_ensure();
	if (retc != XY_OK) {
		XY_SET_ERR(retc);
		return retc;
	}
	if (!xy.adapter) {
		XY_SET_ERR(XY_ERR_INVALID);
		return XY_ERR_INVALID;
	}
	/* T1.5: ran stored in TLS; read ret bytes from TLS retp pointer */
	if (!xy_last_ran) {
		XY_SET_ERR(XY_ERR_NOTFOUND);
		return XY_ERR_NOTFOUND;
	}
	if (ret && xy_last_retp) {
		memcpy(ret, xy_last_retp, xy.adapter->ret_size);
	}
	XY_SET_ERR(XY_OK);
	return XY_OK;
}

/* -------------------------------------------------------------------------
 * xy_call — region-aware dispatch
 * ------------------------------------------------------------------------- */

/*
 * fn_cache_resolve — lazy per-module, per-hook dlsym cache.
 *
 * DISPATCH-SAFETY INVARIANT: this helper is called from inside the DFS
 * dispatch loop of xy_call. It MUST NOT read xy_last_adapter.* — that TLS
 * slot is overwritten whenever a module body nested-calls another hook via
 * xy_call, and we are mid-iteration of the *outer* call.
 *
 * Caller must supply hook_id and name from a stable source on its stack.
 *
 * Returns resolved callback, or NULL if the module does not export this
 * hook. Returns (void *)-1 (cast via XY_FN_RESOLVE_OOM) if cache growth
 * fails; caller treats this as abort (returns XY_ERR_INVALID).
 */
#define XY_FN_RESOLVE_OOM ((void *)(uintptr_t)-1)

static inline void * __attribute__((always_inline, hot))
fn_cache_resolve(xy_mod_entry_t *me, int hook_id, const char *name)
{
	if (likely(hook_id >= 0 && hook_id < me->fn_cache_cap)) {
		void *cb = me->fn_cache[hook_id];
		if (likely(cb)) return cb == XY_FN_NOT_FOUND ? NULL : cb;
	} else if (hook_id >= 0) {
		if (module_ensure_fn_cache_cap(me, hook_id + 16) < 0)
			return XY_FN_RESOLVE_OOM;
	}

	void *cb = module_lookup_symbol_raw(me->handle, name);
	if (hook_id >= 0)
		me->fn_cache[hook_id] = cb ? cb : XY_FN_NOT_FOUND;
	if (cb && hook_id >= 0)
		module_mark_hook_implemented(me, hook_id);
	return cb;
}

/*
 * fn_cache_prewarm — eagerly resolve all currently-known hooks for one
 * module. Called at module-load time (T1.1) so the first hot-path call
 * doesn't pay dlsym latency. Also called from xy_areg for every loaded
 * module when a new hook ID is minted, so the cache stays warm as the
 * hook ID space grows.
 *
 * Returns 0 on success, -1 if cache growth (realloc) failed; caller
 * should treat -1 as a soft failure (lazy resolve will retry per-call).
 */
int
fn_cache_prewarm(xy_mod_entry_t *me)
{
	if (!me || !me->handle) return 0;
	int needed = xy_hook_id_counter;
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
		me->fn_cache[hook_id] = cb ? cb : XY_FN_NOT_FOUND;
		if (cb)
			module_mark_hook_implemented(me, hook_id);
	}
	return 0;
}

static int
region_ensure_hook_dispatch_cap(xy_region_entry_t *re, int hook_id)
{
	if (!re || hook_id < 0)
		return XY_ERR_INVALID;
	if (hook_id < re->hook_dispatch_cap)
		return XY_OK;

	int new_cap = hook_id + 16;
	xy_hook_dispatch_cache_t *tmp = realloc(re->hook_dispatch,
		new_cap * sizeof(*tmp));
	if (unlikely(!tmp))
		return XY_ERR_INVALID;

	for (int i = re->hook_dispatch_cap; i < new_cap; i++) {
		tmp[i].slots = NULL;
		tmp[i].count = 0;
		tmp[i].cap = 0;
		tmp[i].region_gen = 0;
	}

	re->hook_dispatch = tmp;
	re->hook_dispatch_cap = new_cap;
	return XY_OK;
}

/* Build the region-local dispatch vector for one hook by filtering the
 * subtree module list down to actual listeners that survive deny checks. */
static int
region_rebuild_hook_dispatch(xy_region_entry_t *re, int hook_id,
                             const char *hook_name,
                             xy_region_entry_t **anc_chain, int anc_n)
{
	if (!re || hook_id < 0 || !hook_name)
		return XY_ERR_INVALID;
	if (re->subtree_mods_dirty && region_rebuild_subtree_mods(re) < 0)
		return XY_ERR_INVALID;
	if (region_ensure_hook_dispatch_cap(re, hook_id) != XY_OK)
		return XY_ERR_INVALID;

	xy_hook_dispatch_cache_t *cache = &re->hook_dispatch[hook_id];
	if (cache->region_gen == re->dispatch_gen)
		return XY_OK;
	cache->count = 0;

	for (int mi = 0; mi < re->subtree_mods_count; mi++) {
		xy_mod_entry_t *me = re->subtree_mods[mi];
		if (!me || !me->ctx)
			continue;
		if (anc_n > 0 && me->load_path) {
			int denied = 0;
			for (int ai = 0; ai < anc_n; ai++) {
				if (module_is_denied(anc_chain[ai], me->load_path)) {
					denied = 1;
					break;
				}
			}
			if (denied)
				continue;
		}

		if (!module_has_hook_implemented(me, hook_id)) {
			void *cb = fn_cache_resolve(me, hook_id, hook_name);
			if (unlikely(cb == XY_FN_RESOLVE_OOM))
				return XY_ERR_INVALID;
			if (!cb)
				continue;
		}
		if (!module_has_hook_implemented(me, hook_id))
			continue;

		if (cache->count >= cache->cap) {
			int new_cap = cache->cap ? cache->cap * 2 : 8;
			xy_dispatch_slot_t *tmp = realloc(cache->slots,
				new_cap * sizeof(*tmp));
			if (unlikely(!tmp))
				return XY_ERR_INVALID;
			cache->slots = tmp;
			cache->cap = new_cap;
		}
		cache->slots[cache->count].me = me;
		cache->slots[cache->count].cb = me->fn_cache[hook_id];
		cache->count++;
	}

	cache->region_gen = re->dispatch_gen;
	return XY_OK;
}

int __attribute__((hot, flatten))
xy_call(void *retp, xy_adapter_t *reg, void *arg)
{
	int ret = XY_OK;

	if (unlikely(!xy_inited)) {
		ret = xy_runtime_ensure();
		if (ret != XY_OK) {
			xy_zero_ret(retp, reg);
			XY_SET_ERR(ret);
			return ret;
		}
	}

	if (!reg) {
		XY_SET_ERR(XY_ERR_NOTFOUND);
		return XY_ERR_NOTFOUND;
	}

	int pre_err = XY_GET_ERR();
	int hook_id = reg->hook_id;
	void (*dispatch_call)(void *, void *, void *) = reg->call;
	uint64_t region_id = xy_current_region_id;
	xy_region_entry_t *caller_region_entry = xy_current_region_entry;
	xy_region_entry_t *anc_chain[65];
	int anc_n = 0;
	unsigned ran = 0;

	if (unlikely(hook_id < 0)) {
		const void *hid_v = qmap_get(hook_id_hd, reg->name);
		if (!hid_v) {
			ret = XY_ERR_NOTFOUND;
			goto fail;
		}
		hook_id = *(const int *)hid_v;
		reg->hook_id = hook_id;
		if (hook_id >= 0 && hook_id < xy_adapter_by_id_cap) {
			const xy_adapter_t *canonical = xy_adapter_by_id[hook_id];
			if (canonical && !dispatch_call) {
				dispatch_call = canonical->call;
				reg->call = canonical->call;
				reg->ret_size = canonical->ret_size;
				reg->arg_size = canonical->arg_size;
			}
		}
	}

	if (unlikely(reg->ret_size > XY_MAX_RET_SIZE)) {
		ret = XY_ERR_TOOBIG;
		goto fail;
	}

	if (unlikely(!dispatch_call && hook_id >= 0 && hook_id < xy_adapter_by_id_cap)) {
		const xy_adapter_t *canonical = xy_adapter_by_id[hook_id];
		if (canonical)
			dispatch_call = canonical->call;
	}
	if (unlikely(!dispatch_call)) {
		ret = XY_ERR_NOTFOUND;
		goto fail;
	}

	if (unlikely(!caller_region_entry))
		caller_region_entry = region_lookup(region_id);

	uint8_t sflags = caller_region_entry ? caller_region_entry->subtree_flags : 0;
	if (unlikely(sflags & XY_SUBTREE_ANY_MASK) && caller_region_entry)
		anc_n = region_ancestor_chain(caller_region_entry, anc_chain, 65);

	if (unlikely(sflags & XY_SUBTREE_SECURITY_MASK)) {
		for (int i = 0; i < anc_n; i++) {
			if (anc_chain[i]->denied_hooks_set &&
			    qmap_get(anc_chain[i]->denied_hooks_set, reg->name)) {
				ret = XY_ERR_EPERM;
				goto fail;
			}
		}
	}

	xy_last_ran = 0;
	xy_last_retp = retp;
	xy.adapter = reg;

	if (caller_region_entry) {
		if (unlikely(caller_region_entry->subtree_mods_dirty) &&
		    region_rebuild_subtree_mods(caller_region_entry) < 0) {
			ret = XY_ERR_INVALID;
			goto fail;
		}
		if (unlikely(hook_id >= caller_region_entry->hook_dispatch_cap) &&
		    region_ensure_hook_dispatch_cap(caller_region_entry, hook_id) != XY_OK) {
			ret = XY_ERR_INVALID;
			goto fail;
		}

		xy_hook_dispatch_cache_t *cache =
			&caller_region_entry->hook_dispatch[hook_id];
		if (unlikely(cache->region_gen != caller_region_entry->dispatch_gen) &&
		    region_rebuild_hook_dispatch(caller_region_entry, hook_id, reg->name,
		                                 anc_chain, anc_n) != XY_OK) {
			ret = XY_ERR_INVALID;
			goto fail;
		}

		xy_dispatch_slot_t *slots = cache->slots;
		int n = cache->count;
		for (int mi = 0; mi < n; mi++) {
			xy_mod_entry_t *me = slots[mi].me;
			if (mi + 1 < n)
				__builtin_prefetch(&slots[mi + 1], 0, 1);
			if (!me || !me->ctx || !slots[mi].cb)
				continue;
			xy_t *ctx = me->ctx;
			ctx->adapter = reg;
			ctx->region_state = me->region_state;

			xy_region_entry_t *prev_rentry = xy_current_region_entry;
			uint64_t region_changed = (ctx->region_id != region_id);
			if (unlikely(region_changed))
				set_current_region(ctx->region_id, me->region_entry);

			dispatch_call(retp, slots[mi].cb, arg);

			if (unlikely(region_changed))
				set_current_region(region_id, prev_rentry);
			ran++;
		}
	}

	xy_last_ran = ran;
	if (!ran) {
		xy_set_last_ret(NULL, reg->ret_size);
		if (retp)
			memset(retp, 0, reg->ret_size);
		XY_SET_ERR(XY_ERR_NOTFOUND);
		return XY_ERR_NOTFOUND;
	}
	xy_set_last_ret(retp, reg->ret_size);
	XY_SET_ERR(XY_OK);
	return XY_OK;

fail:
	xy_last_ran = 0;
	xy_set_last_ret(NULL, reg->ret_size);
	xy_zero_ret(retp, reg);
	xy.adapter = reg;
	XY_SET_ERR(ret);
	return ret;
}

/* -------------------------------------------------------------------------
 * xy_areg
 * ------------------------------------------------------------------------- */

unsigned
xy_areg(char *name, xy_adapter_t *adapter)
{
	int enter_ret = xy_runtime_ensure();
	if (enter_ret != XY_OK) {
		XY_SET_ERR(enter_ret);
		return XY_INVALID;
	}
	if (adapter->ret_size > XY_MAX_RET_SIZE) {
		XY_SET_ERR(XY_ERR_TOOBIG);
		return XY_INVALID;
	}
	const unsigned *existing = qmap_get(sica_hd, name);
	if (existing) {
		unsigned id = *existing;
	    return id;
	}
	qmap_put(sica_hd, name, &adapter);
	/* Assign a monotonic hook ID for the fn_cache index */
	int hook_id = xy_hook_id_counter++;
	qmap_put(hook_id_hd, name, &hook_id);
	/* Write the ID back into the adapter so callers can skip the hash lookup */
	adapter->hook_id = hook_id;
	/* Grow adapter-by-id array and store pointer */
	if (hook_id >= xy_adapter_by_id_cap) {
		int new_cap = hook_id + 16;
		xy_adapter_t **tmp = realloc(xy_adapter_by_id,
		                              new_cap * sizeof(xy_adapter_t *));
		if (unlikely(!tmp)) {
			XY_SET_ERR(XY_ERR_INVALID);
			return XY_ERR_INVALID;
		}
		xy_adapter_by_id = tmp;
		memset(xy_adapter_by_id + xy_adapter_by_id_cap, 0,
		       (new_cap - xy_adapter_by_id_cap) * sizeof(xy_adapter_t *));
		xy_adapter_by_id_cap = new_cap;
	}
	xy_adapter_by_id[hook_id] = adapter;
	/* T1.1: a new hook ID has been minted — eagerly resolve it in every
	 * already-loaded module so the first dispatch finds it cached. */
	if (mod_hd) {
		unsigned c = qmap_iter(mod_hd, NULL, 0);
		const void *k, *v;
		while (qmap_next(&k, &v, c)) {
			xy_mod_entry_t *m = qmap_ptr(v);
			if (m) (void)fn_cache_prewarm(m);
		}
	}
	XY_SET_ERR(XY_OK);
	return 0;
}
