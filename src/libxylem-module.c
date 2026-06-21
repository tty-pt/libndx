#include "libxylem-internal.h"
#include <stdio.h>

static void _dbg(const char *msg) {
	FILE *f = fopen("/tmp/xy_bind.log", "a");
	if (!f) f = fopen("tmp/xy_bind.log", "a");
	if (f) { fputs(msg, f); fclose(f); }
}

int _mod_run(void *sl, const char *symbol) {
	void (*cb)(void) = NULL;

	module_lookup_symbol_fn(sl, symbol, &cb, sizeof(cb));

	if (!cb) {
		WARN("couldn't find %s\n", symbol);
		return XY_ERR_NOTFOUND;
	}

	((mod_cb_t) cb)();
	return XY_OK;
}

#ifdef _WIN32
#define _RTLD_DEFAULT NULL
#else
#define _RTLD_DEFAULT RTLD_DEFAULT
#endif

void _xy_init(void *ptr, const char *fname,
               uint64_t region_id) {
	xy_t *ctx = ptr;
	ctx->call             = xy_call;
	ctx->areg             = xy_areg;
	ctx->load             = xy_load;
	ctx->last             = xy_last;
	ctx->shutdown         = xy_shutdown;
	ctx->module_path      = fname;
	ctx->region_id        = region_id;
	ctx->deny             = xy_deny;
	ctx->require_claim    = xy_require_claim;
	ctx->region_each      = xy_region_each;
	ctx->with_region      = xy_with_region;
	ctx->current_region   = xy_current_region;
	ctx->unload           = xy_unload;
	ctx->reload           = xy_reload;
}

int _mod_load(char *fname) {
	xy_load_txn_t tx = {
		.inherited_region_id = xy_current_region_id,
	};
	int ret = XY_OK;

	_dbg("_mod_load: called\n");
	ret = mod_load_open_handle(&tx, fname);
	if (ret != XY_OK) {
		XY_SET_ERR(ret);
		return ret;
	}
	if (mod_load_try_reuse_existing(&tx)) {
		_dbg("_mod_load: reuse_existing fired\n");
		return XY_OK;
	}

	ret = mod_load_alloc_entry(&tx, fname);
	if (ret != XY_OK)
		goto fail;

	ret = mod_load_publish_entry(&tx);
	if (ret != XY_OK)
		goto fail;

	ret = mod_load_bind_xy(&tx);
	if (ret != XY_OK)
		goto fail;

	ret = mod_load_enter_context(&tx);
	if (ret != XY_OK)
		goto fail;

	ret = mod_load_claim_if_needed(&tx);
	if (ret != XY_OK)
		goto fail;

	xy_mod_count++;
	{
		typedef size_t xy_region_state_size_t(void);
		xy_region_state_size_t *sz_fn = NULL;
		module_lookup_symbol_fn(tx.handle, "xy_region_state_size", &sz_fn, sizeof(sz_fn));
		if (sz_fn) {
			size_t sz = sz_fn();
			if (sz > 0)
				tx.mod_entry->region_state = calloc(1, sz);
		}
	}
	{
		xy_region_entry_t *re = region_lookup(tx.mod_entry->region_id);
		if (re)
			module_region_append(re, tx.mod_entry);
	}
	(void)fn_cache_prewarm(tx.mod_entry);

	ret = mod_load_run_install(&tx);
	if (ret != XY_OK)
		goto fail;

	mod_load_restore_context(&tx);
	module_lookup_result_free(&tx.lookup);
	return XY_OK;

fail:
	return mod_load_abort(&tx, ret);
}

int xy_load(char *fname) {
	int enter_ret = xy_runtime_ensure();
	if (enter_ret != XY_OK) {
		XY_SET_ERR(enter_ret);
		return enter_ret;
	}
	int ret = _mod_load(fname);
	XY_SET_ERR(ret);
	return ret;
}

/* -------------------------------------------------------------------------
 * xy_unload / xy_reload
 * ------------------------------------------------------------------------- */

/*
 * Internal unload: looks up (fname, region_id) and performs the full teardown.
 * Unload always succeeds — no veto.  Children loaded by this module are
 * cascade-unloaded; if a child has other loaders (refcount > 1) it simply
 * has its refcount decremented and stays active.
 */
int
_mod_unload(char *fname, uint64_t region_id)
{
	xy_lookup_result_t lookup = module_lookup_from_fname(fname, region_id);
	if (lookup.err != XY_OK) {
		XY_SET_ERR(lookup.err);
		return lookup.err;
	}
	xy_mod_entry_t *entry = lookup.entry;
	if (!entry) {
		module_lookup_result_free(&lookup);
		XY_SET_ERR(XY_ERR_NOTFOUND);
		return XY_ERR_NOTFOUND;
	}
	module_lookup_result_free(&lookup);

	/* Decrement refcount — only actually unload when it hits zero */
	entry->refcount--;
	if (entry->refcount > 0)
		return XY_OK;

	/* Cascade-unload children that were loaded by this module.
	 * _mod_unload on a child with refcount > 1 simply decrements its
	 * refcount and leaves it active — so shared children are safe. */
	{
		unsigned c = qmap_iter(mod_hd, NULL, 0);
		const void *key, *value;
		/* Collect children first to avoid iterator invalidation */
		xy_mod_entry_t **children = NULL;
		int nchildren = 0, children_cap = 0;
		while (qmap_next(&key, &value, c)) {
			xy_mod_entry_t *m = qmap_ptr(value);
			if (m && m != entry && m->parent_entry == entry) {
				if (nchildren >= children_cap) {
					children_cap = children_cap ? children_cap * 2 : 8;
					children = realloc(children,
					                   children_cap * sizeof(*children));
				}
				children[nchildren++] = m;
			}
		}
		for (int i = 0; i < nchildren; i++) {
			xy_mod_entry_t *child = children[i];
			/* _mod_unload still takes the original module name and appends
			 * the platform suffix internally. */
			const char *child_path = child && child->ctx
				? child->ctx->module_path : NULL;
			if (child_path)
				_mod_unload((char *)child_path, child->region_id);
		}
		free(children);
	}

	module_clear_fn_cache_for_handle(entry);

	if (entry->region_entry && entry->load_path)
		module_remove_path_owned_entries(entry->region_entry,
		                                 entry->load_path,
		                                 entry->handle);

	/* Remove from region's doubly-linked list (O(1)) */
	module_region_detach(entry);

	/* Remove from hash maps */
	qmap_del(mod_by_region_hd, &entry->region_id);
	qmap_del(mod_hd, entry->mod_key);

	xy_mod_count--;

	/* dlclose balances the dlopen refcount.  Because we load with RTLD_NODELETE
	 * the library is NOT physically removed from the address space — that is by
	 * design (adapter objects in .data sections stay valid for sica_hd).  The
	 * logical module entry has been removed above; the .so stays mapped. */
	module_free_entry(entry, 1);
	return XY_OK;
}

int xy_unload(char *fname) {
	int enter_ret = xy_runtime_ensure();
	if (enter_ret != XY_OK) {
		XY_SET_ERR(enter_ret);
		return enter_ret;
	}
	uint64_t region_id = xy_current_region_id;
	int ret = _mod_unload(fname, region_id);
	XY_SET_ERR(ret);
	return ret;
}

int xy_reload(char *fname) {
	int enter_ret = xy_runtime_ensure();
	if (enter_ret != XY_OK) {
		XY_SET_ERR(enter_ret);
		return enter_ret;
	}
	uint64_t region_id = xy_current_region_id;

	/* Find the existing entry to record its position */
	xy_lookup_result_t lookup = module_lookup_from_fname(fname, region_id);
	if (lookup.err != XY_OK) {
		XY_SET_ERR(lookup.err);
		return lookup.err;
	}
	xy_mod_entry_t *existing = lookup.entry;
	if (!existing) {
		module_lookup_result_free(&lookup);
		XY_SET_ERR(XY_ERR_NOTFOUND);
		return XY_ERR_NOTFOUND;
	}
	module_lookup_result_free(&lookup);

	/* Save position in dispatch list */
	xy_mod_entry_t *insert_after = existing->region_prev; /* may be NULL (was head) */
	xy_region_entry_t *re = existing->region_entry;

	/* Unload — this removes the entry from the list */
	int uret = _mod_unload(fname, region_id);
	if (uret != XY_OK) {
		XY_SET_ERR(uret);
		return uret;
	}

	/* Load the module again */
	int lret = _mod_load(fname);
	if (lret != XY_OK) {
		XY_SET_ERR(lret);
		return lret;
	}

	/* Re-find the newly loaded entry */
	lookup = module_lookup_from_fname(fname, region_id);
	if (lookup.err != XY_OK) {
		XY_SET_ERR(lookup.err);
		return lookup.err;
	}
	xy_mod_entry_t *new_entry = lookup.entry;
	if (!new_entry || !re) {
		module_lookup_result_free(&lookup);
		XY_SET_ERR(XY_OK);
		return XY_OK;
	}
	module_lookup_result_free(&lookup);

	module_region_detach(new_entry);
	module_region_insert_after(re, new_entry, insert_after);

	XY_SET_ERR(XY_OK);
	return XY_OK;
}
