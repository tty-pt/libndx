#define _GNU_SOURCE
#include "../include/ttypt/ndx.h"

#ifdef _WIN32
  #include <windows.h>
  #define dlopen(filename, flags) (void*)LoadLibraryA(filename)
  #define dlsym(handle, symbol) GetProcAddress((HMODULE)handle, symbol)
  #define dlclose(handle) FreeLibrary((HMODULE)handle)
  #define dlerror() _win_dlerror()

  static DWORD ndx_err_tls;

  static char err_buf[256];
  static const char* _win_dlerror(void) {
	  DWORD err_code = GetLastError();
	  if (err_code == 0) {
		  return NULL;
	  }
	  memset(err_buf, 0, sizeof(err_buf));
	  FormatMessageA(
		  FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		  NULL, err_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		  err_buf, sizeof(err_buf), NULL);
	  return err_buf;
  }

  #define NDX_SET_ERR(e) TlsSetValue(ndx_err_tls, (LPVOID)(intptr_t)(e))
  #define NDX_GET_ERR() ((int)(intptr_t)TlsGetValue(ndx_err_tls))
#else
#include <dlfcn.h>
  static int ndx_err_val;

  #define NDX_SET_ERR(e) (ndx_err_val = (e))
  #define NDX_GET_ERR() (ndx_err_val)
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <ttypt/qsys.h>
#include <ttypt/qmap.h>

#include "papi.h"

#define MOD_MASK    0x7FFF
#define SICA_MASK   0x7FFF
#define REGION_MASK 0x7FFF

/* Branch-prediction hints — kernel-style */
#ifndef likely
#  define likely(x)   __builtin_expect(!!(x), 1)
#  define unlikely(x) __builtin_expect(!!(x), 0)
#endif

enum opts {
	OPT_DETACH = 1,
};

/* Global qmap handles */
unsigned mod_hd,            /* module key -> ndx_mod_entry_t* */
         mod_by_region_hd,  /* region_id (uint64_t) -> ndx_mod_entry_t* (MULTIVALUE) */
         sica_hd,           /* hook name  -> ndx_adapter_t*   */
         pledge_hd,         /* hook name  -> owner path (global/root pledge) */
         pledge_ids_hd,     /* hook id    -> owner path (global/root pledge) */
         path_intern_hd,    /* canonical module path -> interned path pointer */
         region_hd;         /* region key -> ndx_region_entry_t* */

/* hook name -> int hook_id (monotonically assigned at ndx_areg time) */
static unsigned hook_id_hd;
static int      ndx_hook_id_counter;

/* ndx_adapter_by_id[hook_id] = adapter pointer — O(1) adapter lookup by id */
static ndx_adapter_t **ndx_adapter_by_id;
static int             ndx_adapter_by_id_cap;

/* Sentinel: dlsym was called and returned NULL (hook not implemented in module) */
#define NDX_FN_NOT_FOUND ((void *)(uintptr_t)1)

static uint32_t ndx_ptr_type;
static uint32_t ndx_mod_entry_type;
static uint32_t ndx_region_entry_type;
static uint32_t region_id_type;  /* fixed 8-byte key type for uint64_t region IDs */
static uint32_t ndx_int_type;    /* fixed sizeof(int) value type for hook IDs */

typedef ndx_t* (*get_ndx_func_t)(void);

ndx_t ndx;
static volatile int ndx_inited;
static void __attribute__((cold)) ndx_init_once(void);
static int _mod_unload(char *fname, uint64_t region_id);
static int fn_cache_prewarm(ndx_mod_entry_t *me);
static int module_is_denied(ndx_region_entry_t *re, const char *path);
static inline int ndx_dispatch_module(ndx_mod_entry_t *me,
                                      int hook_id,
                                      const char *name,
                                      ndx_adapter_t *adapter,
                                      void (*call_fn)(void *, void *, void *),
                                      void *retp,
                                      void *arg,
                                      int commit_ret,
                                      uint64_t current_region_id);
static void module_rekey_region_index(ndx_mod_entry_t *me, uint64_t parent_id,
                                      uint64_t child_id);
static void module_rekey_loaded_entry(ndx_mod_entry_t *me, const char *old_key,
                                      const char *load_path, uint64_t parent_id,
                                      uint64_t child_id);

/* Running count of successfully loaded modules (for alloca sizing in ndx_call) */
static int ndx_mod_count;

/* Running count of registered pledges (root + region-scoped combined).
 * Used to skip the pledge_hd hash lookup entirely when no pledges exist. */
static int ndx_pledge_count;

/* T1.5: Per-call mutable state extracted from ndx_last_adapter to
 * avoid the 88-byte memcpy on fast path. ndx_last() reads these instead
 * of the full struct. */
static __thread unsigned ndx_last_ran;
static __thread void    *ndx_last_retp;

/* Thread-local caller identity (for pledge enforcement) */
static __thread const char *ndx_current_caller;

/* Thread-local current region — id and a direct pointer kept in sync.
 * ndx_current_region_entry may be NULL before the first ndx_init_once() call;
 * code that needs the entry must guard or call region_ensure_root() first. */
static __thread uint64_t           ndx_current_region_id = NDX_REGION_ROOT;
static __thread ndx_region_entry_t *ndx_current_region_entry = NULL;

/* Thread-local pointer to the currently-loading module entry.
 * Set during ndx_install() so child ndx_load() calls can register their
 * parent_entry for cascade-unload tracking. */
static __thread ndx_mod_entry_t *ndx_loading_mod = NULL;

/* Keep the id/pointer pair in sync atomically (within a thread). */
static inline void
set_current_region(uint64_t id, ndx_region_entry_t *entry)
{
	ndx_current_region_id = id;
	ndx_current_region_entry = entry;
}

/* -------------------------------------------------------------------------
 * Region ID helpers
 *
 * Region IDs are plain opaque uint64_t prefix values — all 64 bits are
 * the address.  The prefix length (plen) lives in ndx_region_entry_t, not
 * in the ID itself.
 *
 * NDX_REGION_ROOT = 0: plen=0 in its entry, matches everything.
 * ------------------------------------------------------------------------- */

/*
 * Is anc an ancestor-or-equal of child_id?
 * O(1): mask child_id to anc->plen significant bits and compare to anc->id.
 */
static int
region_is_ancestor(const ndx_region_entry_t *anc, uint64_t child_id)
{
	if (anc->plen == 0)
		return 1; /* root matches everything */
	if (anc->plen == 64)
		return anc->id == child_id; /* exact match only */
	uint64_t mask = ~(((uint64_t)1 << (64 - anc->plen)) - 1);
	return (child_id & mask) == anc->id;
}

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static inline void *
qmap_ptr(const void *value)
{
	return value ? *(void * const *) value : NULL;
}

static inline void *
module_lookup_symbol_raw(void *handle, const char *symbol)
{
#ifdef _WIN32
	void *sym = NULL;
	FARPROC fp = GetProcAddress((HMODULE) handle, symbol);
	memcpy(&sym, &fp, sizeof(sym));
	return sym;
#else
	return dlsym(handle, symbol);
#endif
}

static inline int
module_lookup_symbol_fn(void *handle, const char *symbol, void *fn_out, size_t fn_size)
{
	void *sym = module_lookup_symbol_raw(handle, symbol);
	if (!sym)
		return 0;
	memcpy(fn_out, &sym, fn_size);
	return 1;
}

static inline void
ndx_zero_ret(void *retp, const ndx_adapter_t *reg)
{
	if (retp && reg)
		memset(retp, 0, reg->ret_size);
}

static inline int
module_denied_by_ancestors(ndx_mod_entry_t *me,
                           ndx_region_entry_t **anc_chain, int anc_n)
{
	if (!me || !me->load_path)
		return 0;
	for (int i = 0; i < anc_n; i++) {
		if (module_is_denied(anc_chain[i], me->load_path))
			return 1;
	}
	return 0;
}

static inline int __attribute__((always_inline, hot))
dispatch_fast_module(ndx_mod_entry_t *me,
                     ndx_adapter_t *reg,
                     void (*dispatch_call)(void *, void *, void *),
                     void *retp, void *arg, uint64_t region_id,
                     ndx_region_entry_t **anc_chain, int anc_n,
                     int check_denies)
{
	if (!me || !me->indx)
		return 0;
	if (unlikely(check_denies) && module_denied_by_ancestors(me, anc_chain, anc_n))
		return 0;

	int rc = ndx_dispatch_module(me,
	                             reg->hook_id,
	                             reg->name,
	                             reg,
	                             dispatch_call,
	                             retp, arg,
	                             /*commit_ret=*/0,
	                             region_id);
	if (unlikely(rc == NDX_ERR_INVALID))
		return NDX_ERR_INVALID;
	return rc == 0;
}

static int
module_ensure_fn_cache_cap(ndx_mod_entry_t *me, int needed)
{
	if (!me || needed <= me->fn_cache_cap)
		return 0;
	void **tmp = realloc(me->fn_cache, needed * sizeof(void *));
	if (unlikely(!tmp))
		return -1;
	for (int i = me->fn_cache_cap; i < needed; i++)
		tmp[i] = NULL;
	me->fn_cache = tmp;
	me->fn_cache_cap = needed;
	return 0;
}

/* Look up an ndx_region_entry_t by id.  Returns NULL if not found. */
static ndx_region_entry_t *
region_lookup(uint64_t id)
{
	return (ndx_region_entry_t *)qmap_ptr(qmap_get(region_hd, &id));
}

/* Store an ndx_region_entry_t keyed by its id. */
static void
region_store(ndx_region_entry_t *entry)
{
	qmap_put(region_hd, &entry->id, &entry);
}

static void
interceptors_set_hook_id_for_name(const char *hook_name, int hook_id)
{
	unsigned c = qmap_iter(region_hd, NULL, 0);
	const void *key, *value;
	while (qmap_next(&key, &value, c)) {
		ndx_region_entry_t *re = qmap_ptr(value);
		for (ndx_interceptor_entry_t *ic = re ? re->interceptors : NULL;
		     ic; ic = ic->next) {
			if (ic->hook_id < 0 && strcmp(ic->hook_name, hook_name) == 0)
				ic->hook_id = hook_id;
		}
	}
}

static const char *
module_path_intern(char *path)
{
	if (!path)
		return NULL;
	if (!path_intern_hd)
		path_intern_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, MOD_MASK, 0);
	if (!path_intern_hd) {
		free(path);
		return NULL;
	}
	const void *existing = qmap_get(path_intern_hd, path);
	if (existing) {
		const char *interned = qmap_ptr(existing);
		free(path);
		return interned;
	}
	qmap_put(path_intern_hd, path, &path);
	return path;
}

static void
path_intern_free_all(void)
{
	if (!path_intern_hd)
		return;
	qmap_close(path_intern_hd);
	path_intern_hd = 0;
}

static void
pledges_set_hook_id_for_name(const char *hook_name, int hook_id)
{
	if (hook_id < 0)
		return;

	const char *owner;

	owner = qmap_ptr(qmap_get(pledge_hd, hook_name));
	if (owner) {
		if (!pledge_ids_hd)
			pledge_ids_hd = qmap_open(NULL, NULL, ndx_int_type, ndx_ptr_type,
			                         MOD_MASK, 0);
		qmap_put(pledge_ids_hd, &hook_id, &owner);
		qmap_del(pledge_hd, hook_name);
	}

	unsigned c = qmap_iter(region_hd, NULL, 0);
	const void *key, *value;
	while (qmap_next(&key, &value, c)) {
		ndx_region_entry_t *re = qmap_ptr(value);
		if (!re || !re->pledge_hd)
			continue;
		owner = qmap_ptr(qmap_get(re->pledge_hd, hook_name));
		if (!owner)
			continue;
		if (!re->pledge_ids_hd)
			re->pledge_ids_hd = qmap_open(NULL, NULL, ndx_int_type,
			                              ndx_ptr_type, MOD_MASK, 0);
		qmap_put(re->pledge_ids_hd, &hook_id, &owner);
		qmap_del(re->pledge_hd, hook_name);
	}
}

/*
 * Build an ancestor chain for region_id, root-first.
 * Walks the parent pointer chain from region_id up to root,
 * collecting all ancestors (including the region itself),
 * then reverses so root comes first.
 * Returns number of entries filled.
 */
/*
 * Build an ancestor chain for the given entry, root-first.
 * start must be non-NULL; the hash lookup is the caller's responsibility.
 * Returns number of entries filled.
 */
static int
region_ancestor_chain(ndx_region_entry_t *start,
                      ndx_region_entry_t **chain, int chain_cap)
{
	int n = 0;
	ndx_region_entry_t *e = start;
	while (e && n < chain_cap) {
		chain[n++] = e;
		e = e->parent;
	}
	/* Reverse so root (parent==NULL) is first */
	for (int a = 0, b = n - 1; a < b; a++, b--) {
		ndx_region_entry_t *tmp = chain[a];
		chain[a] = chain[b];
		chain[b] = tmp;
	}
	return n;
}

/* Propagate a subtree flag upward from entry to root. */
static inline void
region_propagate_deny(ndx_region_entry_t *entry)
{
	for (ndx_region_entry_t *e = entry; e; e = e->parent)
		e->subtree_flags |= NDX_SUBTREE_HAS_DENY;
}

static inline void
region_propagate_pledge(ndx_region_entry_t *entry)
{
	for (ndx_region_entry_t *e = entry; e; e = e->parent)
		e->subtree_flags |= NDX_SUBTREE_HAS_PLEDGE;
}

static inline void
region_propagate_interceptors(ndx_region_entry_t *entry)
{
	for (ndx_region_entry_t *e = entry; e; e = e->parent)
		e->subtree_flags |= NDX_SUBTREE_HAS_INTERCEPTORS;
}

static void
deny_list_free(ndx_deny_entry_t *head)
{
	while (head) {
		ndx_deny_entry_t *next = head->next;
		free(head);
		head = next;
	}
}

static void
deny_list_free_owned(ndx_deny_entry_t *head)
{
	while (head) {
		ndx_deny_entry_t *next = head->next;
		free(head->value);
		free(head);
		head = next;
	}
}

static void
interceptor_list_free(ndx_interceptor_entry_t *head)
{
	while (head) {
		ndx_interceptor_entry_t *next = head->next;
		free(head->hook_name);
		free(head);
		head = next;
	}
}

static void
region_entry_free(ndx_region_entry_t *e)
{
	if (!e) return;
	deny_list_free_owned(e->denied_hooks);
	deny_list_free(e->denied_modules);
	interceptor_list_free(e->interceptors);
	if (e->pledge_ids_hd)
		qmap_close(e->pledge_ids_hd);
	if (e->pledge_hd)
		qmap_close(e->pledge_hd);
	free(e->subtree_mods);
	free(e);
}

/* T1.2: mark the subtree module cache dirty along the ancestor chain from
 * `entry` (inclusive) up to the root. Called whenever a module is added or
 * removed from any region, or when a new child region is claimed. O(depth). */
static inline void
region_mark_subtree_dirty(ndx_region_entry_t *entry)
{
	for (ndx_region_entry_t *e = entry; e; e = e->parent)
		e->subtree_mods_dirty = 1;
}

/* Rebuild region->subtree_mods[] in DFS order (region's own modules first,
 * then each child's subtree recursively). Grows the backing array via tmp
 * pointer for OOM safety. Returns 0 on success, -1 on allocation failure. */
static int
region_rebuild_subtree_mods(ndx_region_entry_t *root)
{
	if (!root) return 0;
	root->subtree_mods_count = 0;

	/* DFS with an explicit stack — same 65-slot bound as before. */
	ndx_region_entry_t *stk[65];
	int stk_top = 0;
	stk[stk_top++] = root;

	while (stk_top > 0) {
		ndx_region_entry_t *rn = stk[--stk_top];

		for (ndx_mod_entry_t *me = (ndx_mod_entry_t *)rn->mods_head;
		     me; me = me->region_next) {
			if (root->subtree_mods_count >= root->subtree_mods_cap) {
				int new_cap = root->subtree_mods_cap
					? root->subtree_mods_cap * 2 : 8;
				ndx_mod_entry_t **tmp = realloc(root->subtree_mods,
					new_cap * sizeof(*tmp));
				if (unlikely(!tmp)) return -1;
				root->subtree_mods = tmp;
				root->subtree_mods_cap = new_cap;
			}
			root->subtree_mods[root->subtree_mods_count++] = me;
		}

		for (ndx_region_entry_t *ch = rn->children_head;
		     ch && stk_top < 65; ch = ch->sibling_next)
			stk[stk_top++] = ch;
	}

	root->subtree_mods_dirty = 0;
	return 0;
}

/* Ensure the root region entry exists */
static void
region_ensure_root(void)
{
	ndx_region_entry_t *root = region_lookup(NDX_REGION_ROOT);
	if (!root) {
		root = calloc(1, sizeof(*root));
		root->id         = NDX_REGION_ROOT;
		root->depth      = 0;
		root->child_bits = 0;
		root->plen       = 0;
		root->owner_path = NULL; /* host owns root */
		root->parent     = NULL; /* root has no parent */
		region_store(root);
	}
	/* Always sync the thread-local pointer to the current root entry */
	if (!ndx_current_region_entry || ndx_current_region_id == NDX_REGION_ROOT)
		ndx_current_region_entry = root;
}

/* -------------------------------------------------------------------------
 * Module key helpers
 *
 * mod_hd is keyed by "path\0<region_hex>" to allow same .so in multiple
 * regions (each gets its own ndx_mod_entry_t).
 * ------------------------------------------------------------------------- */

static void
mod_key(char *buf, size_t buf_len, const char *path, uint64_t region_id)
{
	snprintf(buf, buf_len, "%s%c%016llx",
	         path, '\0', (unsigned long long)region_id);
}

/* We need a key length that includes the embedded NUL — use fixed 17 suffix */
#define MOD_KEY_SUFFIX_LEN 17  /* NUL + 16 hex digits */

static size_t
mod_key_len(const char *path)
{
	return strlen(path) + MOD_KEY_SUFFIX_LEN;
}

/*
 * Resolve the actual shared-library path used for identity and dlopen.
 * We keep module_path in ndx_t as the caller's original fname, but module
 * identity is keyed by the resolved file so aliases like "../mods/song" and
 * "mods/song" collapse to the same entry.
 */
static char *
module_load_path(const char *fname)
{
#ifdef _WIN32
	const char *ext = ".dll";
	size_t flen = strlen(fname);
	size_t elen = strlen(ext);
	char *buf = malloc(flen + elen + 1);
	if (!buf)
		return NULL;
	memcpy(buf, fname, flen);
	memcpy(buf + flen, ext, elen + 1);

	DWORD need = GetFullPathNameA(buf, 0, NULL, NULL);
	if (need == 0)
		return buf;

	char *full = malloc((size_t)need + 1);
	if (!full) {
		free(buf);
		return NULL;
	}
	if (!GetFullPathNameA(buf, need + 1, full, NULL)) {
		free(buf);
		free(full);
		return NULL;
	}
	free(buf);
	return full;
#else
	const char *ext = ".so";
	size_t flen = strlen(fname);
	size_t elen = strlen(ext);
	char *buf = malloc(flen + elen + 1);
	if (!buf)
		return NULL;
	memcpy(buf, fname, flen);
	memcpy(buf + flen, ext, elen + 1);
	char *resolved = realpath(buf, NULL);
	if (resolved) {
		free(buf);
		return resolved;
	}
	return buf;
#endif
}

/*
 * Build the canonical module key for a (fname, region_id) pair.
 * The caller owns the returned path buffer and must free it.
 */
static char *
module_key_from_fname(char *buf, size_t buf_len,
                      const char *fname, uint64_t region_id)
{
	char *load_path = module_load_path(fname);
	if (!load_path)
		return NULL;
	mod_key(buf, buf_len, load_path, region_id);
	return load_path;
}

typedef struct {
	ndx_mod_entry_t *entry;
	char             key[512];
	char            *load_path;
	int              err;
} ndx_lookup_result_t;

static void
module_lookup_result_free(ndx_lookup_result_t *lookup)
{
	if (!lookup)
		return;
	free(lookup->load_path);
	lookup->load_path = NULL;
}

static ndx_lookup_result_t
module_lookup_from_fname(const char *fname, uint64_t region_id)
{
	ndx_lookup_result_t out = {0};
	out.load_path = module_key_from_fname(out.key, sizeof(out.key),
	                                      fname, region_id);
	if (!out.load_path) {
		out.err = NDX_ERR_INVALID;
		return out;
	}
	out.entry = qmap_ptr(qmap_get(mod_hd, out.key));
	out.err = NDX_OK;
	return out;
}

static void
module_rekey_for_claim(const char *caller, uint64_t parent_id,
                       uint64_t child_id)
{
	ndx_lookup_result_t lookup = module_lookup_from_fname(caller, parent_id);
	if (!lookup.load_path)
		return;

	if (lookup.entry)
		module_rekey_loaded_entry(lookup.entry, lookup.key, lookup.load_path,
		                          parent_id, child_id);
	module_lookup_result_free(&lookup);
}

static void
module_rekey_region_index(ndx_mod_entry_t *me, uint64_t parent_id,
                          uint64_t child_id)
{
	/* mod_by_region_hd is QM_MULTIVALUE; collect all entries for parent_id,
	 * delete them all, re-insert all except me under parent_id, then insert
	 * me under child_id. */
#define MOD_BY_REGION_MAX 512
	ndx_mod_entry_t *others[MOD_BY_REGION_MAX];
	int nothers = 0;
	uint32_t cur = qmap_iter(mod_by_region_hd, &parent_id, 0);
	const void *ck, *cv;
	while (qmap_next(&ck, &cv, cur)) {
		ndx_mod_entry_t *e = qmap_ptr(cv);
		if (e && e != me && nothers < MOD_BY_REGION_MAX)
			others[nothers++] = e;
	}
	qmap_del_all(mod_by_region_hd, &parent_id);
	for (int oi = 0; oi < nothers; oi++)
		qmap_put(mod_by_region_hd, &parent_id, &others[oi]);
	qmap_put(mod_by_region_hd, &child_id, &me);
#undef MOD_BY_REGION_MAX
}

static void
module_rekey_loaded_entry(ndx_mod_entry_t *me, const char *old_key,
                          const char *load_path, uint64_t parent_id,
                          uint64_t child_id)
{
	if (!me || !old_key || !load_path) {
		return;
	}

	me->region_id = child_id;
	char new_key[256];
	mod_key(new_key, sizeof(new_key), load_path, child_id);
	qmap_put(mod_hd, new_key, &me);
	qmap_del(mod_hd, old_key);
	module_rekey_region_index(me, parent_id, child_id);
	if (me->indx)
		me->indx->region_id = child_id;
}

static void *
module_base_from_symbol(void *sym)
{
#ifdef _WIN32
	HMODULE mod = NULL;
	if (!sym)
		return NULL;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        (LPCSTR) sym, &mod))
		return NULL;
	return (void *) mod;
#else
	Dl_info di;
	if (!sym || !dladdr(sym, &di))
		return NULL;
	return di.dli_fbase;
#endif
}

static void *
module_handle_base(void *handle)
{
#ifdef _WIN32
	return handle;
#else
	void *any_sym = module_lookup_symbol_raw(handle, "ndx_install");
	if (!any_sym) any_sym = module_lookup_symbol_raw(handle, "get_ndx_ptr");
	return module_base_from_symbol(any_sym);
#endif
}

static void
module_remove_owner_keys(unsigned map_hd, const char *path, int *count)
{
	unsigned pc = qmap_iter(map_hd, NULL, 0);
	const void *pk, *pv;
	const char **del_keys = NULL;
	int ndel = 0, del_cap = 0;
	while (qmap_next(&pk, &pv, pc)) {
		const char *owner = qmap_ptr(pv);
		if (owner && strcmp(owner, path) == 0) {
			if (ndel >= del_cap) {
				del_cap = del_cap ? del_cap * 2 : 4;
				del_keys = realloc(del_keys, del_cap * sizeof(char *));
			}
			del_keys[ndel++] = (const char *)pk;
		}
	}
	for (int i = 0; i < ndel; i++) {
		qmap_del(map_hd, del_keys[i]);
		if (count)
			(*count)--;
	}
	free(del_keys);
}

static void
module_remove_denies(ndx_region_entry_t *re, const char *path)
{
	if (!re || !path)
		return;
	ndx_deny_entry_t **pp = &re->denied_modules;
	while (*pp) {
		if ((*pp)->value == path) {
			ndx_deny_entry_t *dead = *pp;
			*pp = dead->next;
			if (re->denied_modules_set)
				qmap_del(re->denied_modules_set, &dead->value);
			free(dead);
		} else {
			pp = &(*pp)->next;
		}
	}
}

static int
module_is_denied(ndx_region_entry_t *re, const char *path)
{
	if (!re || !path)
		return 0;
	if (re->denied_modules_set && qmap_get(re->denied_modules_set, &path))
		return 1;
	for (ndx_deny_entry_t *d = re->denied_modules; d; d = d->next) {
		if (d->value == path)
			return 1;
	}
	return 0;
}

static void
module_remove_interceptors(ndx_region_entry_t *re, void *base)
{
	if (!re || !base)
		return;

	ndx_interceptor_entry_t **ip = &re->interceptors;
	while (*ip) {
		void *fn_ptr;
		memcpy(&fn_ptr, &(*ip)->fn, sizeof(fn_ptr));
		if (module_base_from_symbol(fn_ptr) == base) {
			ndx_interceptor_entry_t *dead = *ip;
			*ip = dead->next;
			free(dead->hook_name);
			free(dead);
			continue;
		}
		ip = &(*ip)->next;
	}
}

static void
module_region_detach(ndx_mod_entry_t *entry)
{
	ndx_region_entry_t *re = entry ? entry->region_entry : NULL;
	if (!re)
		return;

	if (entry->region_prev)
		entry->region_prev->region_next = entry->region_next;
	else
		re->mods_head = entry->region_next;

	if (entry->region_next)
		entry->region_next->region_prev = entry->region_prev;
	else
		re->mods_tail = entry->region_prev;

	entry->region_prev = NULL;
	entry->region_next = NULL;
	region_mark_subtree_dirty(re);
}

static void
module_region_insert_after(ndx_region_entry_t *re, ndx_mod_entry_t *entry,
                           ndx_mod_entry_t *after)
{
	if (!re || !entry)
		return;

	entry->region_entry = re;
	if (!after) {
		entry->region_prev = NULL;
		entry->region_next = re->mods_head;
		if (re->mods_head)
			re->mods_head->region_prev = entry;
		else
			re->mods_tail = entry;
		re->mods_head = entry;
	} else {
		entry->region_prev = after;
		entry->region_next = after->region_next;
		if (after->region_next)
			after->region_next->region_prev = entry;
		else
			re->mods_tail = entry;
		after->region_next = entry;
	}
	region_mark_subtree_dirty(re);
}

static void
module_region_append(ndx_region_entry_t *re, ndx_mod_entry_t *entry)
{
	module_region_insert_after(re, entry, re ? re->mods_tail : NULL);
}

static void
module_clear_fn_cache_for_handle(ndx_mod_entry_t *entry)
{
	if (!entry || !entry->handle)
		return;
	void *base = module_handle_base(entry->handle);
	if (!base)
		return;

	unsigned c = qmap_iter(mod_hd, NULL, 0);
	const void *key, *value;
	while (qmap_next(&key, &value, c)) {
		ndx_mod_entry_t *m = qmap_ptr(value);
		if (!m || m == entry) continue;
		for (int i = 0; i < m->fn_cache_cap; i++) {
			void *fp = m->fn_cache[i];
			if (!fp || fp == NDX_FN_NOT_FOUND) continue;
			if (module_base_from_symbol(fp) == base)
				m->fn_cache[i] = NULL; /* force re-resolve */
		}
	}
}

static int
mod_load_abort(ndx_mod_entry_t *mod_entry, char *stable_fname,
               const char *stable_key, void *sl, int err)
{
	if (mod_entry) {
		qmap_del(mod_by_region_hd, &mod_entry->region_id);
		qmap_del(mod_hd, stable_key);
		free(mod_entry->fn_cache);
		free(mod_entry->mod_key);
		free(mod_entry);
	}
	free(stable_fname);
	dlclose(sl);
	return err;
}

/*
 * Remove all entries owned by a module path from a region and from the root
 * pledge map.  The caller passes the region entry and path because unload
 * already has both on hand.
 */
static void
module_remove_path_owned_entries(ndx_region_entry_t *re, const char *path,
                                 void *handle)
{
	if (!re || !path)
		return;

	module_remove_denies(re, path);
	module_remove_interceptors(re, module_handle_base(handle));
	module_remove_owner_keys(re->pledge_hd, path, &ndx_pledge_count);
	module_remove_owner_keys(pledge_hd, path, &ndx_pledge_count);
}

/*
 * Variable-length key type for mod_hd.
 *
 * Keys are "path\0<16-hex-region>" — the embedded NUL means QM_STR would
 * truncate to just the path, making all regions of the same .so collide.
 * qmap_mreg registers a type whose measure callback returns the full key
 * length (strlen stops at the NUL, giving path length; adding
 * MOD_KEY_SUFFIX_LEN accounts for the NUL + 16 hex chars).
 * qmap then hashes/compares via memcmp over the full blob, correctly
 * distinguishing (path, region_A) from (path, region_B).
 */
static size_t
mod_key_measure(const void *data)
{
	return strlen((const char *)data) + MOD_KEY_SUFFIX_LEN;
}

static uint32_t mod_key_type = QM_MISS;

/* -------------------------------------------------------------------------
 * Public API: error
 * ------------------------------------------------------------------------- */

int ndx_errno(void) {
	return NDX_GET_ERR();
}

const char *ndx_strerror(int err) {
	switch (err) {
	case NDX_OK:           return "success";
	case NDX_ERR_NOTFOUND: return "not found";
	case NDX_ERR_INVALID:  return "invalid argument";
	case NDX_ERR_TOOBIG:   return "return type too large";
	case NDX_ERR_INIT:     return "initialization failed";
	case NDX_ERR_EPERM:    return "operation not permitted";
	default:               return "unknown error";
	}
}

/* -------------------------------------------------------------------------
 * Caller / region identity
 * ------------------------------------------------------------------------- */

void ndx_set_caller(const char *module_path) {
	ndx_current_caller = module_path;
}

/* -------------------------------------------------------------------------
 * Pledge (region-scoped; caller's current region at pledge time)
 * ------------------------------------------------------------------------- */

int ndx_pledge(const char *hook_name) {
	ndx_init_once();
	const char *caller = ndx_current_caller;
	if (!caller) {
		NDX_SET_ERR(NDX_ERR_INVALID);
		return NDX_ERR_INVALID;
	}
	const void *hid_v = qmap_get(hook_id_hd, hook_name);
	int hook_id = hid_v ? *(const int *)hid_v : -1;

	/* Scope: the caller's current region */
	uint64_t scope = ndx_current_region_id;

	if (scope == NDX_REGION_ROOT) {
		/* Root-scope: use the legacy global pledge map */
		if ((hook_id >= 0 && pledge_ids_hd && qmap_get(pledge_ids_hd, &hook_id)) ||
		    qmap_get(pledge_hd, hook_name)) {
			NDX_SET_ERR(NDX_ERR_EPERM);
			return NDX_ERR_EPERM;
		}
		if (hook_id >= 0) {
			if (!pledge_ids_hd)
				pledge_ids_hd = qmap_open(NULL, NULL, ndx_int_type,
				                          ndx_ptr_type, MOD_MASK, 0);
			qmap_put(pledge_ids_hd, &hook_id, &caller);
		} else {
			qmap_put(pledge_hd, hook_name, &caller);
		}
		ndx_pledge_count++;
		NDX_SET_ERR(NDX_OK);
		return NDX_OK;
	}

	/* Non-root scope: use the region's pledge map */
	ndx_region_entry_t *reg = region_lookup(scope);
	if (!reg) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}
	if (!reg->pledge_hd) {
		reg->pledge_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type,
		                           MOD_MASK, 0);
	}
	if ((hook_id >= 0 && reg->pledge_ids_hd &&
	     qmap_get(reg->pledge_ids_hd, &hook_id)) ||
	    qmap_get(reg->pledge_hd, hook_name)) {
		NDX_SET_ERR(NDX_ERR_EPERM);
		return NDX_ERR_EPERM;
	}
	if (hook_id >= 0) {
		if (!reg->pledge_ids_hd) {
			reg->pledge_ids_hd = qmap_open(NULL, NULL, ndx_int_type,
			                               ndx_ptr_type, MOD_MASK, 0);
		}
		qmap_put(reg->pledge_ids_hd, &hook_id, &caller);
	} else {
		qmap_put(reg->pledge_hd, hook_name, &caller);
	}
	region_propagate_pledge(reg);
	ndx_pledge_count++;
	NDX_SET_ERR(NDX_OK);
	return NDX_OK;
}

/* -------------------------------------------------------------------------
 * ndx_on_claim — register a claim handler on the caller's current region
 * ------------------------------------------------------------------------- */

int ndx_require_claim(ndx_claim_handler_fn_t *fn, void *ud) {
	ndx_init_once();
	ndx_region_entry_t *reg = region_lookup(ndx_current_region_id);
	if (!reg) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}
	if (!fn) {
		/* NULL handler clears the require-claim gate */
		reg->claim_handler    = NULL;
		reg->claim_handler_ud = NULL;
		reg->require_claim    = 0;
	} else {
		reg->claim_handler    = fn;
		reg->claim_handler_ud = ud;
		reg->require_claim    = 1;
	}
	NDX_SET_ERR(NDX_OK);
	return NDX_OK;
}

/* -------------------------------------------------------------------------
 * ndx_claim — claim a child region under the caller's current region
 * ------------------------------------------------------------------------- */

/*
 * Find the first free slot of 'bits' width under parent entry.
 * A slot is free if no existing region has the same plen and id value.
 * Returns the child ID (plain 64-bit prefix value), or NDX_REGION_INVALID.
 */
static uint64_t
region_alloc_slot(const ndx_region_entry_t *parent, uint8_t bits)
{
	uint8_t  cplen = parent->plen + bits;

	if (cplen > 64)
		return NDX_REGION_INVALID;

	uint64_t num_slots = (cplen == 64) ? (uint64_t)0 /* wraps */ : (uint64_t)1 << bits;
	/* When cplen==64, there is exactly 1 slot (the full 64-bit value);
	 * handle via slot_shift==0 special case below. */
	uint64_t slot_shift = (cplen < 64) ? (64 - cplen) : 0;

	/* Iterate all possible slot offsets: 0 .. (1<<bits)-1.
	 * For cplen==64, num_slots wrapped to 0 so we use a do-while for
	 * exactly one iteration. */
	uint64_t s = 0;
	do {
		uint64_t candidate_id = parent->id | (s << slot_shift);

		/* O(1) collision check via the region hash map */
		if (!region_lookup(candidate_id))
			return candidate_id;
		s++;
	} while (s != num_slots);

	return NDX_REGION_INVALID;
}

/* -------------------------------------------------------------------------
 * _ndx_claim_for_load — internal helper: perform a claim on behalf of a
 * module being loaded.  Called from _mod_load when the module has an
 * ndx_claim data symbol and the parent region has require_claim set.
 *
 * caller     — stable module path (stable_fname)
 * parent_id  — region the module is being loaded into
 * bits       — value read from the module's ndx_claim symbol
 * handle     — dlopen handle so we can update the live ndx_t
 * ------------------------------------------------------------------------- */
static int
_ndx_claim_for_load(const char *caller, uint64_t parent_id,
                    uint8_t bits, void *handle)
{
	(void)handle;
	if (bits == 0 || bits > 64) {
		NDX_SET_ERR(NDX_ERR_INVALID);
		return NDX_ERR_INVALID;
	}

	ndx_region_entry_t *parent = region_lookup(parent_id);
	if (!parent) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

	/* Invoke the parent's claim handler */
	if (!parent->claim_handler) {
		NDX_SET_ERR(NDX_ERR_EPERM);
		return NDX_ERR_EPERM;
	}

	uint8_t granted = 0;
	int hret = parent->claim_handler(caller ? caller : "",
	                                  bits, &granted,
	                                  parent->claim_handler_ud);
	if (hret != NDX_OK) {
		NDX_SET_ERR(NDX_ERR_EPERM);
		return NDX_ERR_EPERM;
	}
	if (granted == 0 || granted > 64) {
		NDX_SET_ERR(NDX_ERR_EPERM);
		return NDX_ERR_EPERM;
	}

	/* Find a free slot */
	uint64_t child_id = region_alloc_slot(parent, granted);
	if (child_id == NDX_REGION_INVALID) {
		NDX_SET_ERR(NDX_ERR_TOOBIG);
		return NDX_ERR_TOOBIG;
	}

	/* Create the child region entry */
	ndx_region_entry_t *child = calloc(1, sizeof(*child));
	child->id         = child_id;
	child->plen       = parent->plen + granted;
	child->depth      = parent->depth + 1;
	child->child_bits = granted;
	child->owner_path = caller;
	child->parent     = parent;
	region_store(child);

	/* Wire child into parent's children list */
	child->sibling_next   = (ndx_region_entry_t *)parent->children_head;
	parent->children_head = child;
	region_mark_subtree_dirty(parent);

	/* Update the thread-local region context so ndx_install sees child */
	set_current_region(child_id, child);

	/* Update the mod entry and re-key it under the new region */
	if (caller) {
		module_rekey_for_claim(caller, parent_id, child_id);
	}

	NDX_SET_ERR(NDX_OK);
	return NDX_OK;
}

/* -------------------------------------------------------------------------
 * Region deny API
 * ------------------------------------------------------------------------- */

int ndx_deny(const char *what, ndx_deny_type_t type) {
	ndx_init_once();
	ndx_region_entry_t *reg = region_lookup(ndx_current_region_id);
	if (!reg) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

	ndx_deny_entry_t *node = malloc(sizeof(*node));
	node->value = strdup(what);

	if (type == NDX_DENY_HOOK) {
		node->next = reg->denied_hooks;
		reg->denied_hooks = node;
		if (!reg->denied_hooks_set)
			reg->denied_hooks_set = qmap_open(NULL, NULL, QM_STR,
			                                  ndx_ptr_type, MOD_MASK, 0);
		{
			void *sentinel = (void *)(uintptr_t)1;
			qmap_put(reg->denied_hooks_set, node->value, &sentinel);
		}
	} else {
		char *path = module_load_path(what);
		if (!path) {
			free(node);
			NDX_SET_ERR(NDX_ERR_INVALID);
			return NDX_ERR_INVALID;
		}
		const char *interned = module_path_intern(path);
		if (!interned) {
			free(node);
			NDX_SET_ERR(NDX_ERR_INVALID);
			return NDX_ERR_INVALID;
		}
		node->value = (char *)interned;
		node->next = reg->denied_modules;
		reg->denied_modules = node;
		if (!reg->denied_modules_set)
			reg->denied_modules_set = qmap_open(NULL, NULL, ndx_ptr_type,
			                                    ndx_ptr_type, MOD_MASK, 0);
		{
			void *sentinel = (void *)(uintptr_t)1;
			qmap_put(reg->denied_modules_set, &node->value, &sentinel);
		}
	}
	region_propagate_deny(reg);

	NDX_SET_ERR(NDX_OK);
	return NDX_OK;
}

/* -------------------------------------------------------------------------
 * Region interceptor registration
 * ------------------------------------------------------------------------- */

int ndx_intercept(const char *hook_name,
                  ndx_interceptor_fn_t *fn, void *ud) {
	ndx_init_once();
	ndx_region_entry_t *reg = region_lookup(ndx_current_region_id);
	if (!reg) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

	ndx_interceptor_entry_t *node = malloc(sizeof(*node));
	node->hook_name = strdup(hook_name);
	node->hook_id   = -1;
	node->fn        = fn;
	node->ud        = ud;
	const void *hid_v = qmap_get(hook_id_hd, hook_name);
	if (hid_v)
		node->hook_id = *(const int *)hid_v;
	/* Prepend; chain is walked root-first so we reverse at call time */
	node->next = reg->interceptors;
	reg->interceptors = node;
	region_propagate_interceptors(reg);

	NDX_SET_ERR(NDX_OK);
	return NDX_OK;
}

/* -------------------------------------------------------------------------
 * ndx_region_each — enumerate immediate children of caller's current region
 * ------------------------------------------------------------------------- */

int ndx_region_each(ndx_region_each_fn_t *fn, void *ud) {
	ndx_init_once();
	uint64_t parent_id = ndx_current_region_id;
	ndx_region_entry_t *parent = region_lookup(parent_id);
	if (!parent) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

	unsigned c = qmap_iter(region_hd, NULL, 0);
	const void *key, *value;
	int ret = NDX_OK;
	while (qmap_next(&key, &value, c)) {
		ndx_region_entry_t *e = qmap_ptr(value);
		if (!e || e->id == parent_id) continue;
		if (!region_is_ancestor(parent, e->id)) continue;
		/* Immediate child: plen == parent->plen + child_bits */
		if (e->plen != (uint8_t)(parent->plen + e->child_bits)) continue;
		ret = fn(e->id, ud);
		if (ret != NDX_OK) break;
	}
	NDX_SET_ERR(NDX_OK);
	return ret;
}

uint64_t
ndx_current_region(void)
{
	ndx_init_once();
	return ndx_current_region_id;
}

int
ndx_with_region(uint64_t region_id, ndx_scope_fn_t *fn, void *ud)
{
	ndx_init_once();
	if (!fn) {
		NDX_SET_ERR(NDX_ERR_INVALID);
		return NDX_ERR_INVALID;
	}

	ndx_region_entry_t *target = region_lookup(region_id);
	if (!target) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

	uint64_t prev_region_id = ndx_current_region_id;
	ndx_region_entry_t *prev_region_entry = ndx_current_region_entry;
	set_current_region(region_id, target);

	int ret = fn(ud);

	set_current_region(prev_region_id, prev_region_entry);
	NDX_SET_ERR(ret == NDX_OK ? NDX_OK : ret);
	return ret;
}

/* -------------------------------------------------------------------------
 * Module loading
 * ------------------------------------------------------------------------- */

int _mod_run(void *sl, char *symbol) {
	void (*cb)(void) = NULL;

	module_lookup_symbol_fn(sl, symbol, &cb, sizeof(cb));

	if (!cb) {
		WARN("couldn't find %s\n", symbol);
		return NDX_ERR_NOTFOUND;
	}

	((mod_cb_t) cb)();
	return NDX_OK;
}

#ifdef _WIN32
#define _RTLD_DEFAULT NULL
#else
#define _RTLD_DEFAULT RTLD_DEFAULT
#endif

void _ndx_init(void *ptr, const char *fname,
               uint64_t region_id) {
	ndx_t *indx = ptr;
	indx->call             = ndx_call;
	indx->areg             = ndx_areg;
	indx->load             = ndx_load;
	indx->last             = ndx_last;
	indx->shutdown         = ndx_shutdown;
	indx->module_path      = fname;
	indx->pledge           = ndx_pledge;
	indx->set_caller       = ndx_set_caller;
	indx->region_id        = region_id;
	indx->deny             = ndx_deny;
	indx->intercept        = ndx_intercept;
	indx->require_claim    = ndx_require_claim;
	indx->region_each      = ndx_region_each;
	indx->with_region      = ndx_with_region;
	indx->current_region   = ndx_current_region;
	indx->unload           = ndx_unload;
	indx->reload           = ndx_reload;
}

int _mod_load(char *fname) {
	char *symbol;
	void *sl;
	uint64_t inherited_region_id = ndx_current_region_id;

	ndx_lookup_result_t lookup = module_lookup_from_fname(fname, inherited_region_id);
	if (lookup.err != NDX_OK) {
		NDX_SET_ERR(lookup.err);
		return lookup.err;
	}

	char *load_path = lookup.load_path;
	if (!load_path) {
		NDX_SET_ERR(NDX_ERR_INVALID);
		return NDX_ERR_INVALID;
	}

	sl = dlopen(load_path, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);

	if (!sl) {
		WARN("_mod_load failed loading '%s': %s\n", fname, dlerror());
		module_lookup_result_free(&lookup);
		return NDX_ERR_NOTFOUND;
	}

	/* Key by (path, region_id) — allows same .so in multiple regions */
	ndx_mod_entry_t *existing = lookup.entry;
	if (existing) {
		existing->refcount++;
		dlclose(sl); /* balance the extra dlopen; RTLD_NODELETE keeps it loaded */
		module_lookup_result_free(&lookup);
		return NDX_OK;
	}

	symbol = "ndx_install";
	// WARN("%s: '%s'\n", symbol, fname);

	/* Duplicate fname so module_path remains valid after the caller's buffer
	 * is freed (e.g. alloca'd in a parent frame). */
	char *stable_fname = strdup(fname);
	/* Duplicate the composite key too */
	size_t klen = mod_key_len(load_path);
	char *stable_key = malloc(klen);
	memcpy(stable_key, lookup.key, klen);
	const char *interned_load_path = module_path_intern(load_path);
	if (!interned_load_path) {
		free(stable_key);
		free(stable_fname);
		dlclose(sl);
		module_lookup_result_free(&lookup);
		return NDX_ERR_INVALID;
	}

	/* Register module early so concurrent ndx_loads see the entry. */
	ndx_mod_entry_t *mod_entry = calloc(1, sizeof(*mod_entry));
	mod_entry->handle       = sl;
	mod_entry->region_id    = inherited_region_id;
	mod_entry->indx         = NULL;
	mod_entry->refcount     = 1;
	mod_entry->parent_entry = ndx_loading_mod;
	mod_entry->mod_key      = stable_key;
	mod_entry->load_path    = interned_load_path;
	qmap_put(mod_hd, stable_key, &mod_entry);

	get_ndx_func_t get_ndx = NULL;
	module_lookup_symbol_fn(sl, "get_ndx_ptr", &get_ndx, sizeof(get_ndx));
	if (get_ndx) {
		ndx_t *indx = get_ndx();
		_ndx_init(indx, stable_fname, inherited_region_id);
		mod_entry->indx = indx;
	}

	/* Insert into secondary region index */
	qmap_put(mod_by_region_hd, &mod_entry->region_id, &mod_entry);

	/* ------------------------------------------------------------------
	 * ndx_claim symbol handling:
	 *
	 * 1. If the parent region has require_claim AND the module lacks the
	 *    ndx_claim symbol → reject immediately (NDX_ERR_EPERM).
	 * 2. If the module has an ndx_claim symbol AND the parent has a claim
	 *    handler → perform auto-claim before ndx_install.
	 * 3. If the module has ndx_claim but the parent has no handler AND
	 *    no require_claim → ignore the symbol and load normally.
	 * ------------------------------------------------------------------ */
	ndx_region_entry_t *inherited_reg = region_lookup(inherited_region_id);
	uint8_t *claim_sym = module_lookup_symbol_raw(sl, "ndx_claim");

	if (inherited_reg && inherited_reg->require_claim && !claim_sym) {
		/* No ndx_claim symbol — reject */
		NDX_SET_ERR(NDX_ERR_EPERM);
		return mod_load_abort(mod_entry, stable_fname, stable_key, sl,
		                      NDX_ERR_EPERM);
	}

	/* Set thread-local region context for the duration of ndx_install so
	 * any ndx_load() calls inside install inherit the correct region. */
	uint64_t            prev_region_id    = ndx_current_region_id;
	ndx_region_entry_t *prev_region_entry = ndx_current_region_entry;
	const char         *prev_caller       = ndx_current_caller;

	set_current_region(inherited_region_id, inherited_reg);
	ndx_current_caller = stable_fname;

	/* Auto-claim: if the module has ndx_claim AND the parent has a handler,
	 * perform the claim on behalf of the module before running ndx_install.
	 * If parent has require_claim but no handler, the claim will fail and
	 * the load is rejected. */
	int do_claim = claim_sym && inherited_reg &&
	               (inherited_reg->claim_handler || inherited_reg->require_claim);
	if (do_claim) {
		int cret = _ndx_claim_for_load(stable_fname, inherited_region_id,
		                                *claim_sym, sl);
		if (cret != NDX_OK) {
			set_current_region(prev_region_id, prev_region_entry);
			ndx_current_caller = prev_caller;
			return mod_load_abort(mod_entry, stable_fname, stable_key, sl, cret);
		}
		/* _ndx_claim_for_load updated ndx_current_region via set_current_region */
	}

	ndx_mod_entry_t *prev_loading_mod = ndx_loading_mod;
	ndx_loading_mod = mod_entry;
	int runret = _mod_run(sl, symbol);
	ndx_loading_mod = prev_loading_mod;

	/* Restore context */
	set_current_region(prev_region_id, prev_region_entry);
	ndx_current_caller = prev_caller;

	if (runret != NDX_OK) {
		return mod_load_abort(mod_entry, stable_fname, stable_key, sl, runret);
	}

	ndx_mod_count++;

	/* Allocate per-region state if the module exports ndx_region_state_size */
	{
		typedef size_t ndx_region_state_size_t(void);
		ndx_region_state_size_t *sz_fn = NULL;
		module_lookup_symbol_fn(sl, "ndx_region_state_size", &sz_fn, sizeof(sz_fn));
		if (sz_fn) {
			size_t sz = sz_fn();
			if (sz > 0)
				mod_entry->region_state = calloc(1, sz);
		}
	}

	/* Append mod_entry to its region's module list (preserves load order).
	 * When do_claim ran, mod_entry->region_id was updated to the child region,
	 * so this correctly appends to the child region's list.
	 * Also cache the region_entry pointer for O(1) dispatch saves/restores. */
	{
		ndx_region_entry_t *re = region_lookup(mod_entry->region_id);
		if (re) {
			module_region_append(re, mod_entry);
		}
	}

	/* T1.1: pre-resolve all known hooks for this module so the first
	 * dispatch call doesn't pay dlsym latency. Best-effort: failure is
	 * tolerated since fn_cache_resolve will retry lazily. */
	(void)fn_cache_prewarm(mod_entry);

	return NDX_OK;
}

int ndx_load(char *fname) {
	ndx_init_once();
	int ret = _mod_load(fname);
	NDX_SET_ERR(ret);
	return ret;
}

/* -------------------------------------------------------------------------
 * ndx_unload / ndx_reload
 * ------------------------------------------------------------------------- */

/*
 * Internal unload: looks up (fname, region_id) and performs the full teardown.
 * Unload always succeeds — no veto.  Children loaded by this module are
 * cascade-unloaded; if a child has other loaders (refcount > 1) it simply
 * has its refcount decremented and stays active.
 */
static int
_mod_unload(char *fname, uint64_t region_id)
{
	ndx_lookup_result_t lookup = module_lookup_from_fname(fname, region_id);
	if (lookup.err != NDX_OK) {
		NDX_SET_ERR(lookup.err);
		return lookup.err;
	}
	ndx_mod_entry_t *entry = lookup.entry;
	if (!entry) {
		module_lookup_result_free(&lookup);
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}
	module_lookup_result_free(&lookup);

	/* Decrement refcount — only actually unload when it hits zero */
	entry->refcount--;
	if (entry->refcount > 0)
		return NDX_OK;

	/* Cascade-unload children that were loaded by this module.
	 * _mod_unload on a child with refcount > 1 simply decrements its
	 * refcount and leaves it active — so shared children are safe. */
	{
		unsigned c = qmap_iter(mod_hd, NULL, 0);
		const void *key, *value;
		/* Collect children first to avoid iterator invalidation */
		ndx_mod_entry_t **children = NULL;
		int nchildren = 0, children_cap = 0;
		while (qmap_next(&key, &value, c)) {
			ndx_mod_entry_t *m = qmap_ptr(value);
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
			ndx_mod_entry_t *child = children[i];
			/* _mod_unload still takes the original module name and appends
			 * the platform suffix internally. */
			const char *child_path = child && child->indx
				? child->indx->module_path : NULL;
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

	ndx_mod_count--;

	/* Release resources */
	void *handle = entry->handle;
	/* Per-region state cleanup */
	if (entry->region_state) {
		typedef void ndx_region_cleanup_t(void *state);
		ndx_region_cleanup_t *cleanup_fn = NULL;
		module_lookup_symbol_fn(entry->handle, "ndx_region_cleanup",
		                        &cleanup_fn, sizeof(cleanup_fn));
		if (cleanup_fn)
			cleanup_fn(entry->region_state);
		free(entry->region_state);
	}

	free(entry->fn_cache);
	free(entry->mod_key);
	free(entry);

	/* dlclose balances the dlopen refcount.  Because we load with RTLD_NODELETE
	 * the library is NOT physically removed from the address space — that is by
	 * design (adapter objects in .data sections stay valid for sica_hd).  The
	 * logical module entry has been removed above; the .so stays mapped. */
	dlclose(handle);
	return NDX_OK;
}

int ndx_unload(char *fname) {
	ndx_init_once();
	uint64_t region_id = ndx_current_region_id;
	int ret = _mod_unload(fname, region_id);
	NDX_SET_ERR(ret);
	return ret;
}

int ndx_reload(char *fname) {
	ndx_init_once();
	uint64_t region_id = ndx_current_region_id;

	/* Find the existing entry to record its position */
	ndx_lookup_result_t lookup = module_lookup_from_fname(fname, region_id);
	if (lookup.err != NDX_OK) {
		NDX_SET_ERR(lookup.err);
		return lookup.err;
	}
	ndx_mod_entry_t *existing = lookup.entry;
	if (!existing) {
		module_lookup_result_free(&lookup);
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}
	module_lookup_result_free(&lookup);

	/* Save position in dispatch list */
	ndx_mod_entry_t *insert_after = existing->region_prev; /* may be NULL (was head) */
	ndx_region_entry_t *re = existing->region_entry;

	/* Unload — this removes the entry from the list */
	int uret = _mod_unload(fname, region_id);
	if (uret != NDX_OK) {
		NDX_SET_ERR(uret);
		return uret;
	}

	/* Load the module again */
	int lret = _mod_load(fname);
	if (lret != NDX_OK) {
		NDX_SET_ERR(lret);
		return lret;
	}

	/* Re-find the newly loaded entry */
	lookup = module_lookup_from_fname(fname, region_id);
	if (lookup.err != NDX_OK) {
		NDX_SET_ERR(lookup.err);
		return lookup.err;
	}
	ndx_mod_entry_t *new_entry = lookup.entry;
	if (!new_entry || !re) {
		module_lookup_result_free(&lookup);
		NDX_SET_ERR(NDX_OK);
		return NDX_OK;
	}
	module_lookup_result_free(&lookup);

	module_region_detach(new_entry);
	module_region_insert_after(re, new_entry, insert_after);

	NDX_SET_ERR(NDX_OK);
	return NDX_OK;
}

int ndx_last(void *ret) {
	ndx_init_once();
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
 * Interceptor chain state passed through the middleware.
 */
typedef struct {
	ndx_interceptor_entry_t **chain;
	int                       chain_len;
	int                       chain_pos;
	const char               *hook_name;
	ndx_adapter_t            *adapter;
	uint64_t                  region_id;
	unsigned                 *ran_out;
	ndx_mod_entry_t         **entries;
	int                       entry_count;
	int                       hook_id;   /* pre-resolved at ndx_call time */
	int                       abort_err; /* non-zero → abort the chain (OOM etc.) */
} ndx_dispatch_ctx_t;

/* Keep dispatch context compact; grows only with deliberate review. */
_Static_assert(sizeof(ndx_dispatch_ctx_t) <= 72,
               "ndx_dispatch_ctx_t grew — review hot-path cache footprint");

/*
 * fn_cache_resolve — lazy per-module, per-hook dlsym cache.
 *
 * DISPATCH-SAFETY INVARIANT: this helper is called from inside the DFS
 * dispatch loops of both the fast path (ndx_call) and the slow path
 * (dispatch_next). It MUST NOT read ndx_last_adapter.* — that TLS slot is
 * overwritten whenever a module body nested-calls another hook via
 * ndx_call, and we are mid-iteration of the *outer* call.
 *
 * Caller must supply hook_id and name from a stable source: the caller's
 * stack (reg->hook_id / reg->name in the fast path) or a ctx-local adapter
 * copy (ctx->adapter->hook_id / ctx->adapter->name in the slow path).
 *
 * Returns resolved callback, or NULL if the module does not export this
 * hook. Returns (void *)-1 (cast via NDX_FN_RESOLVE_OOM) if cache growth
 * fails; caller treats this as abort (fast path returns NDX_ERR_INVALID,
 * slow path sets ctx->abort_err).
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
static int
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
	}
	return 0;
}

/*
 * ndx_dispatch_module — single per-module dispatch step shared by the
 * fast path (ndx_call inner DFS loop) and the slow path (dispatch_next).
 *
 * always_inline guarantees zero call overhead at both sites; commit_ret
 * is a compile-time constant per call site so its branch is folded away.
 *
 * Returns:
 *   0                  → ran successfully (caller bumps ran counter)
 *  -1                  → skipped (module doesn't export this hook, or
 *                        me->indx is NULL)
 *   NDX_ERR_INVALID    → fn_cache growth failed; caller propagates per
 *                        its dispatch context (fast path: set errno +
 *                        return; slow path: set ctx->abort_err + return)
 *
 * DISPATCH-SAFETY INVARIANT: helper takes hook_id/name/adapter as
 * parameters from caller's stack/ctx. It does NOT read ndx_last_adapter.
 * See fn_cache_resolve comment.
 */
static inline int __attribute__((always_inline, hot))
ndx_dispatch_module(ndx_mod_entry_t *me,
                    int              hook_id,
                    const char      *name,
	ndx_adapter_t   *adapter,
	void           (*call_fn)(void *, void *, void *),
	void            *retp,
	void            *arg,
	int              commit_ret,
	uint64_t         current_region_id)
{
	ndx_t *indx = me->indx;
	if (unlikely(!indx)) return -1;

	void *cb = fn_cache_resolve(me, hook_id, name);
	if (unlikely(cb == NDX_FN_RESOLVE_OOM)) return NDX_ERR_INVALID;
	if (!cb) return -1;

	indx->adapter      = adapter;
	indx->region_state = me->region_state;

	ndx_region_entry_t *prev_rentry = ndx_current_region_entry;
	uint64_t region_changed = (indx->region_id != current_region_id);
	if (unlikely(region_changed))
		set_current_region(indx->region_id, me->region_entry);

	call_fn(retp, cb, arg);

	if (unlikely(region_changed))
		set_current_region(current_region_id, prev_rentry);

	if (commit_ret)
		memcpy(adapter->ret, retp, adapter->ret_size);

	return 0;
}

static void dispatch_next(void *args, void *ret, void *ud);

static void
dispatch_next(void *args, void *ret, void *ud)
{
	ndx_dispatch_ctx_t *ctx = ud;

	if (ctx->chain_pos < ctx->chain_len) {
		ndx_interceptor_entry_t *ic = ctx->chain[ctx->chain_pos++];
		ic->fn(ctx->hook_name, args, ret,
		       dispatch_next, ctx, ic->ud);
		return;
	}

	for (int i = 0; i < ctx->entry_count; i++) {
		ndx_mod_entry_t *me = ctx->entries[i];
		int rc = ndx_dispatch_module(me,
		                             ctx->adapter->hook_id,
		                             ctx->adapter->name,
		                             ctx->adapter,
		                             ctx->adapter->call,
		                             ret, args,
		                             /*commit_ret=*/1,
		                             ctx->region_id);
		if (unlikely(rc == NDX_ERR_INVALID)) {
			ctx->abort_err = NDX_ERR_INVALID;
			return;
		}
		if (rc == 0) (*ctx->ran_out)++;
	}
}

static void __attribute__((cold, noinline))
ndx_warn_pledge_root(const char *caller, const char *name, const char *owner)
{
	WARN("ndx_call: pledge violation — '%s' called '%s' (owner: '%s')\n",
		caller ? caller : "(unknown)", name, owner);
}

static void __attribute__((cold, noinline))
ndx_warn_pledge_region(const char *caller, const char *name,
                       unsigned long long rid, const char *owner)
{
	WARN("ndx_call: pledge violation — '%s' called '%s' in region %llu"
	     " (owner: '%s')\n",
	     caller ? caller : "(unknown)", name, rid, owner);
}

int __attribute__((hot, flatten))
ndx_call(void *retp, ndx_adapter_t *reg, void *arg, const char *caller)
{
	uint64_t region_id = ndx_current_region_id;
	if (unlikely(!ndx_inited))
		ndx_init_once();

	if (!reg) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

	int pre_err = NDX_GET_ERR();
	int ret = NDX_OK;
	int pledge_hook_id = reg->hook_id;
	if (unlikely(ndx_pledge_count > 0 && pledge_hook_id < 0)) {
		const void *hid_v = qmap_get(hook_id_hd, reg->name);
		if (hid_v)
			pledge_hook_id = *(const int *)hid_v;
	}

	/* ------------------------------------------------------------------
	 * 1. Root-scoped pledge check (skipped when no pledges registered).
	 *    Caller identity is only written to TLS when actually needed.
	 * ------------------------------------------------------------------ */
	if (unlikely(ndx_pledge_count > 0)) {
		ndx_current_caller = caller;
		const char *global_pledge_owner = NULL;
		if (pledge_hook_id >= 0 && pledge_ids_hd)
			global_pledge_owner = qmap_ptr(qmap_get(pledge_ids_hd, &pledge_hook_id));
		if (!global_pledge_owner)
			global_pledge_owner = qmap_ptr(qmap_get(pledge_hd, reg->name));
		if (global_pledge_owner) {
			const char *caller = ndx_current_caller;
			if (!caller || strcmp(caller, global_pledge_owner) != 0) {
				ndx_warn_pledge_root(caller, reg->name, global_pledge_owner);
				ret = NDX_ERR_EPERM;
				goto fail;
			}
		}
	}

	/* ------------------------------------------------------------------
	 * 2. Security pre-check using cached subtree flags.
	 *
	 * Each ndx_region_entry_t carries subtree_has_* flags that are set
	 * whenever a deny/pledge/interceptor is registered anywhere in the
	 * subtree rooted at that entry.  Reading three bytes off the caller's
	 * cached region pointer (ndx_current_region_entry) tells us whether we need
	 * to do any further work — no ancestor walk required on the clean path.
	 * ------------------------------------------------------------------ */
	ndx_region_entry_t *caller_region_entry = ndx_current_region_entry;
	if (unlikely(!caller_region_entry))
		caller_region_entry = region_lookup(region_id);

	/* Single-byte subtree flag load — replaces three-field OR dance.
	 * Common case: subtree is clean, flags==0, both checks fold to one
	 * AND + jz away from the fast path. */
	uint8_t sflags = caller_region_entry ? caller_region_entry->subtree_flags : 0;
	int has_deny_pledge  = (sflags & NDX_SUBTREE_SECURITY_MASK) != 0;
	int has_interceptors = (sflags & NDX_SUBTREE_HAS_INTERCEPTORS) != 0;

	/* Ancestor chain — built lazily, only when security checks are needed */
	ndx_region_entry_t *anc_chain[65];
	int anc_n = 0;

	if (unlikely(sflags & NDX_SUBTREE_ANY_MASK)) {
		if (caller_region_entry)
			anc_n = region_ancestor_chain(caller_region_entry, anc_chain, 65);
	}

	if (unlikely(has_deny_pledge)) {
		/* Caller identity needed for pledge checks in this branch */
		ndx_current_caller = caller;
		for (int i = 0; i < anc_n; i++) {
			/* Denied hook? Fires for any call into the denying region or its descendants. */
			if (anc_chain[i]->denied_hooks_set &&
			    qmap_get(anc_chain[i]->denied_hooks_set, reg->name)) {
				ret = NDX_ERR_EPERM;
				goto fail;
			}
			/* Region-scoped pledge check */
			if (anc_chain[i]->pledge_ids_hd || anc_chain[i]->pledge_hd) {
				const char *rp_owner = NULL;
				if (pledge_hook_id >= 0 && anc_chain[i]->pledge_ids_hd)
					rp_owner = qmap_ptr(qmap_get(anc_chain[i]->pledge_ids_hd,
					                             &pledge_hook_id));
				if (!rp_owner && anc_chain[i]->pledge_hd)
					rp_owner = qmap_ptr(qmap_get(anc_chain[i]->pledge_hd, reg->name));
				if (rp_owner) {
					if (!caller || strcmp(caller, rp_owner) != 0) {
						ndx_warn_pledge_region(caller, reg->name,
						     (unsigned long long)anc_chain[i]->id, rp_owner);
						ret = NDX_ERR_EPERM;
						goto fail;
					}
				}
			}
		}
	}

	/* ------------------------------------------------------------------
	 * 4. Adapter is already provided by the caller (reg parameter).
	 *    hook_id is stored in reg->hook_id — no hash lookup needed on the
	 *    hot path.  When hook_id < 0 the adapter is a NDX_DECL stub that
	 *    hasn't been resolved yet; do a one-time name lookup and write the
	 *    id back so subsequent calls are O(1).
	 * ------------------------------------------------------------------ */
	int hook_id = reg->hook_id;
	if (unlikely(hook_id < 0)) {
		const void *hid_v = qmap_get(hook_id_hd, reg->name);
		if (!hid_v) {
			ret = NDX_ERR_NOTFOUND;
			goto fail;
		}
		hook_id = *(const int *)hid_v;
		reg->hook_id = hook_id; /* write back — next call is O(1) */
		/* Also sync call ptr and ret_size from the canonical adapter */
		if (hook_id >= 0 && hook_id < ndx_adapter_by_id_cap) {
			const ndx_adapter_t *canonical = ndx_adapter_by_id[hook_id];
			if (canonical && !reg->call) {
				reg->call     = canonical->call;
				reg->ret_size = canonical->ret_size;
				reg->arg_size = canonical->arg_size;
			}
		}
		/* Bounds-check on first resolve only — ret_size doesn't change
		 * after registration, so subsequent hot-path calls skip this. */
		if (unlikely(reg->ret_size > NDX_MAX_RET_SIZE)) {
			ret = NDX_ERR_TOOBIG;
			goto fail;
		}
	}

	/* ------------------------------------------------------------------
	 * 5 + 6 + 7. DFS + interceptor collection + dispatch
	 *
	 * Fast path (no interceptors — the common case):
	 *   Dispatch each module inline during the DFS walk.
	 *   No alloca, no adapter metadata copy, one pass.
	 *   adapter.ret is written once at the end (not per-module).
	 *
	 * Slow path (interceptors present):
	 *   Collect entries[] first, then run through the middleware chain.
	 *   Uses a local adapter copy so middleware can read/write adapter.ret.
	 * ------------------------------------------------------------------ */
/* Resolve dispatch call pointer into a stack local so nested ndx_call
		 * invocations (from module bodies) cannot clobber what we dispatch.
		 * ndx_last_* TLS is shared across nested calls; dispatch_call is stack-local. */
		void (*dispatch_call)(void *, void *, void *) = reg->call;
	if (unlikely(!dispatch_call && hook_id >= 0 && hook_id < ndx_adapter_by_id_cap)) {
		const ndx_adapter_t *canonical = ndx_adapter_by_id[hook_id];
		if (canonical) dispatch_call = canonical->call;
	}
	if (unlikely(!dispatch_call)) {
		ret = NDX_ERR_NOTFOUND;
		goto fail;
	}

	if (likely(!has_interceptors)) {
		/* ---- Fast path: inline DFS dispatch (no interceptors) ---- */
		unsigned ran = 0;

		/* Pre-populate ndx_last_adapter metadata so ndx.adapter is valid
		 * during module execution (modules may call ndx.last() mid-dispatch).
		 * T1.5: point ndx.adapter directly at reg; store mutable per-call
		 * state (ran, retp) in TLS to avoid the 88-byte memcpy. */
		ndx_last_ran = 0;
		ndx_last_retp = retp;
		ndx.adapter = (ndx_adapter_t *)reg;

		if (caller_region_entry) {
			if (unlikely(caller_region_entry->subtree_mods_dirty)) {
				if (region_rebuild_subtree_mods(caller_region_entry) < 0) {
					ret = NDX_ERR_INVALID;
					goto fail;
				}
			}
			ndx_mod_entry_t **mods = caller_region_entry->subtree_mods;
			int n = caller_region_entry->subtree_mods_count;
			if (likely(n == 1)) {
				int did_run = dispatch_fast_module(mods[0], (ndx_adapter_t *)reg,
				                                   dispatch_call, retp, arg,
				                                   region_id, anc_chain, anc_n,
				                                   has_deny_pledge);
				if (unlikely(did_run == NDX_ERR_INVALID)) {
					ret = NDX_ERR_INVALID;
					goto fail;
				}
				ran += did_run;
			} else if (likely(n == 2)) {
				for (int mi = 0; mi < 2; mi++) {
					int did_run = dispatch_fast_module(mods[mi], (ndx_adapter_t *)reg,
					                                   dispatch_call, retp, arg,
					                                   region_id, anc_chain, anc_n,
					                                   has_deny_pledge);
					if (unlikely(did_run == NDX_ERR_INVALID)) {
						ret = NDX_ERR_INVALID;
						goto fail;
					}
					ran += did_run;
				}
			} else {
				for (int mi = 0; mi < n; mi++) {
					ndx_mod_entry_t *me = mods[mi];
					/* Prefetch next module's hot cache line */
					if (mi + 1 < n)
						__builtin_prefetch(mods[mi + 1], 0, 1);
					int did_run = dispatch_fast_module(me, (ndx_adapter_t *)reg,
					                                   dispatch_call, retp, arg,
					                                   region_id, anc_chain, anc_n,
					                                   has_deny_pledge);
					if (unlikely(did_run == NDX_ERR_INVALID)) {
						ret = NDX_ERR_INVALID;
						goto fail;
					}
					ran += did_run;
				}
			}
		}

		/* Commit ran and final ret value to TLS (ndx.last() reads from here) */
		ndx_last_ran = ran;
		ndx_last_retp = ran ? retp : NULL;

	} else {
		/* ---- Slow path: collect entries[], build interceptor chain ---- */
		int total_mods = ndx_mod_count;
		ndx_mod_entry_t **entries = total_mods > 0
			? alloca(total_mods * sizeof(ndx_mod_entry_t *)) : NULL;
		int entry_count = 0;

		if (caller_region_entry) {
			if (unlikely(caller_region_entry->subtree_mods_dirty)) {
				if (region_rebuild_subtree_mods(caller_region_entry) < 0) {
					ret = NDX_ERR_INVALID;
					goto fail;
				}
			}
			ndx_mod_entry_t **mods = caller_region_entry->subtree_mods;
			int n = caller_region_entry->subtree_mods_count;
			for (int mi = 0; mi < n; mi++) {
				ndx_mod_entry_t *me = mods[mi];
				if (mi + 1 < n)
					__builtin_prefetch(mods[mi + 1], 0, 1);
				if (!me->indx) continue;
				const char *mpath = me->load_path;
				int denied = 0;
				for (int i = 0; i < anc_n && !denied; i++) {
					if (module_is_denied(anc_chain[i], mpath))
						denied = 1;
				}
				if (!denied && entry_count < total_mods)
					entries[entry_count++] = me;
			}
		}

#define MAX_INTERCEPTORS 256
		ndx_interceptor_entry_t *ic_chain[MAX_INTERCEPTORS];
		int ic_n = 0;

		for (int ai = 0; ai < anc_n && ic_n < MAX_INTERCEPTORS; ai++) {
			ndx_region_entry_t *anc = anc_chain[ai];
			int reg_ic_start = ic_n;
			for (ndx_interceptor_entry_t *ic = anc->interceptors; ic; ic = ic->next) {
				if (ic->hook_id == hook_id &&
				    ic_n < MAX_INTERCEPTORS) {
					ic_chain[ic_n++] = ic;
				}
			}
			for (int a = reg_ic_start, b = ic_n - 1; a < b; a++, b--) {
				ndx_interceptor_entry_t *tmp = ic_chain[a];
				ic_chain[a] = ic_chain[b];
				ic_chain[b] = tmp;
			}
		}

		/* Local adapter copy needed so middleware can read/write adapter.ret */
		ndx_adapter_t adapter;
		memcpy(&adapter, reg, offsetof(ndx_adapter_t, ret));
		adapter.ran = 0;
		/* Ensure adapter.call is populated even when caller passed a DECL
		 * stub whose .call is NULL — dispatch_next invokes ctx->adapter->call. */
		if (unlikely(!adapter.call))
			adapter.call = dispatch_call;

		ndx_dispatch_ctx_t ctx = {
			.chain        = ic_chain,
			.chain_len    = ic_n,
			.chain_pos    = 0,
			.hook_name    = reg->name,
			.adapter      = &adapter,
			.region_id    = region_id,
			.ran_out      = &adapter.ran,
			.entries      = entries,
			.entry_count  = entry_count,
			.hook_id      = hook_id,
			.abort_err    = 0,
		};

		dispatch_next(arg, retp, &ctx);

		if (unlikely(ctx.abort_err)) {
			ret = ctx.abort_err;
			goto fail;
		}

		/* Commit to TLS for ndx.last() */
		ndx_last_ran = adapter.ran;
		ndx_last_retp = adapter.ret;
	}

	if (!ndx_last_ran && retp)
		memset(retp, 0, reg->ret_size);

	ndx.adapter = (ndx_adapter_t *)reg;
	if (pre_err != NDX_OK && NDX_GET_ERR() == pre_err)
		NDX_SET_ERR(NDX_OK);
	return NDX_OK;

fail:
	ndx_zero_ret(retp, reg);
	NDX_SET_ERR(ret);
	return ret;
}

/* -------------------------------------------------------------------------
 * ndx_areg
 * ------------------------------------------------------------------------- */

unsigned
ndx_areg(char *name, ndx_adapter_t *adapter)
{
	ndx_init_once();
	if (adapter->ret_size > NDX_MAX_RET_SIZE) {
		NDX_SET_ERR(NDX_ERR_TOOBIG);
		return NDX_INVALID;
	}
	const unsigned *existing = qmap_get(sica_hd, name);
	if (existing)
	    return *existing;
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
	interceptors_set_hook_id_for_name(name, hook_id);
	pledges_set_hook_id_for_name(name, hook_id);
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

/* -------------------------------------------------------------------------
 * ndx_shutdown
 * ------------------------------------------------------------------------- */

void
ndx_shutdown(void)
{
	{
		unsigned c = qmap_iter(mod_hd, NULL, 0);
		const void *key, *value;
		while (qmap_next(&key, &value, c)) {
			ndx_mod_entry_t *entry = qmap_ptr(value);
			if (!entry)
				continue;
			free(entry->fn_cache);
			dlclose(entry->handle);
		}
	}
	path_intern_free_all();
	/* Free region entries */
	{
		unsigned c = qmap_iter(region_hd, NULL, 0);
		const void *key, *value;
		while (qmap_next(&key, &value, c)) {
			ndx_region_entry_t *e = qmap_ptr(value);
			region_entry_free(e);
		}
	}
	qmap_close(mod_by_region_hd);
	mod_by_region_hd = 0;
	/* Reset thread-local region pointer — entries were freed above */
	ndx_current_region_entry = NULL;
	ndx_current_region_id = NDX_REGION_ROOT;
	/* Reset TLS state for ndx.last() */
	ndx_last_ran = 0;
	ndx_last_retp = NULL;
	ndx_inited = 0;
}

/* -------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------- */

static void
shared_init(void)
{
	ndx.areg             = ndx_areg;
	ndx.call             = ndx_call;
	ndx.load             = ndx_load;
	ndx.err              = ndx_errno;
	ndx.strerror         = ndx_strerror;
	ndx.pledge           = ndx_pledge;
	ndx.set_caller       = ndx_set_caller;
	ndx.deny             = ndx_deny;
	ndx.intercept        = ndx_intercept;
	ndx.require_claim    = ndx_require_claim;
	ndx.region_each      = ndx_region_each;
	ndx.with_region      = ndx_with_region;
	ndx.current_region   = ndx_current_region;
	ndx.unload           = ndx_unload;
	ndx.reload           = ndx_reload;
}

static void __attribute__((cold))
ndx_init_once(void)
{
	if (likely(ndx_inited))
		return;
	ndx_inited = 1;

	if (!ndx_ptr_type)
		ndx_ptr_type = qmap_reg(sizeof(void *));

	if (!ndx_mod_entry_type)
		ndx_mod_entry_type = qmap_reg(sizeof(ndx_mod_entry_t *));

	if (!ndx_region_entry_type)
		ndx_region_entry_type = qmap_reg(sizeof(ndx_region_entry_t *));

	if (mod_key_type == QM_MISS)
		mod_key_type = qmap_mreg(mod_key_measure);

	if (!region_id_type)
		region_id_type = qmap_reg(sizeof(uint64_t));

	if (!ndx_int_type)
		ndx_int_type = qmap_reg(sizeof(int));

	if (!sica_hd)
		sica_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, SICA_MASK, 0);
	if (!hook_id_hd)
		hook_id_hd = qmap_open(NULL, NULL, QM_STR, ndx_int_type, SICA_MASK, 0);
	if (!path_intern_hd)
		path_intern_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, MOD_MASK, 0);
	mod_hd           = qmap_open(NULL, NULL, mod_key_type, ndx_ptr_type, MOD_MASK, 0);
	mod_by_region_hd = qmap_open(NULL, NULL, region_id_type, ndx_ptr_type,
	                             MOD_MASK, QM_SORTED | QM_MULTIVALUE);
	pledge_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, MOD_MASK, 0);
	pledge_ids_hd = qmap_open(NULL, NULL, ndx_int_type, ndx_ptr_type,
	                          MOD_MASK, 0);
	region_hd = qmap_open(NULL, NULL, region_id_type, ndx_ptr_type, REGION_MASK, 0);

	shared_init();
	region_ensure_root();
}

#ifndef _WIN32
__attribute__((constructor))
#endif
  void ndx_init(void)
{
	ndx_init_once();
}
