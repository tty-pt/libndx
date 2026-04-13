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

enum opts {
	OPT_DETACH = 1,
};

/* Global qmap handles */
unsigned mod_hd,       /* module key -> ndx_mod_entry_t* */
         sica_hd,      /* hook name  -> ndx_adapter_t*   */
         pledge_hd,    /* hook name  -> owner path (global/root pledge) */
         region_hd;    /* region key -> ndx_region_entry_t* */

static uint32_t ndx_ptr_type;
static uint32_t ndx_mod_entry_type;
static uint32_t ndx_region_entry_type;

typedef ndx_t* (*get_ndx_func_t)(void);

ndx_t ndx;
static volatile int ndx_inited;
static void ndx_init_once(void);

/* Last-used adapter state, stored persistently so ndx.adapter doesn't dangle */
static ndx_adapter_t ndx_last_adapter;

/* Thread-local caller identity (for pledge enforcement) */
static __thread const char *ndx_current_caller;

/* Thread-local current region */
static __thread uint64_t ndx_current_region_id = NDX_REGION_ROOT;

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

/* Look up an ndx_region_entry_t by id.  Returns NULL if not found. */
static ndx_region_entry_t *
region_lookup(uint64_t id)
{
	char key[20];
	snprintf(key, sizeof(key), "%016llx", (unsigned long long)id);
	return (ndx_region_entry_t *)qmap_ptr(qmap_get(region_hd, key));
}

/* Store an ndx_region_entry_t keyed by its id. */
static void
region_store(ndx_region_entry_t *entry)
{
	char key[20];
	snprintf(key, sizeof(key), "%016llx", (unsigned long long)entry->id);
	qmap_put(region_hd, key, &entry);
}

/*
 * Build an ancestor chain for region_id, root-first.
 * Iterates all known regions and collects those that are ancestors of
 * region_id, then sorts by plen ascending (= root first).
 * Returns number of entries filled (including region_id itself).
 */
static int
region_ancestor_chain(uint64_t id,
                      ndx_region_entry_t **chain, int chain_cap)
{
	int n = 0;
	unsigned c = qmap_iter(region_hd, NULL, 0);
	const void *key, *value;
	while (qmap_next(&key, &value, c)) {
		ndx_region_entry_t *e = qmap_ptr(value);
		if (!e) continue;
		if (!region_is_ancestor(e, id)) continue;
		if (n < chain_cap)
			chain[n++] = e;
	}
	/* Sort by plen ascending (root first) — simple insertion sort */
	for (int i = 1; i < n; i++) {
		ndx_region_entry_t *tmp = chain[i];
		int j = i - 1;
		while (j >= 0 && chain[j]->plen > tmp->plen) {
			chain[j + 1] = chain[j];
			j--;
		}
		chain[j + 1] = tmp;
	}
	return n;
}

/* Check if hook_name is in a deny list */
static int
deny_list_contains(ndx_deny_entry_t *head, const char *value)
{
	for (ndx_deny_entry_t *e = head; e; e = e->next)
		if (strcmp(e->value, value) == 0) return 1;
	return 0;
}

static void
deny_list_free(ndx_deny_entry_t *head)
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
	deny_list_free(e->denied_hooks);
	deny_list_free(e->denied_modules);
	interceptor_list_free(e->interceptors);
	free(e);
}

/* Ensure the root region entry exists */
static void
region_ensure_root(void)
{
	if (!region_lookup(NDX_REGION_ROOT)) {
		ndx_region_entry_t *root = calloc(1, sizeof(*root));
		root->id         = NDX_REGION_ROOT;
		root->depth      = 0;
		root->child_bits = 0;
		root->plen       = 0;
		root->owner_path = NULL; /* host owns root */
		region_store(root);
	}
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

	/* Scope: the caller's current region */
	uint64_t scope = ndx_current_region_id;

	if (scope == NDX_REGION_ROOT) {
		/* Root-scope: use the legacy global pledge map */
		if (qmap_get(pledge_hd, hook_name)) {
			NDX_SET_ERR(NDX_ERR_EPERM);
			return NDX_ERR_EPERM;
		}
		qmap_put(pledge_hd, hook_name, &caller);
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
	if (qmap_get(reg->pledge_hd, hook_name)) {
		NDX_SET_ERR(NDX_ERR_EPERM);
		return NDX_ERR_EPERM;
	}
	qmap_put(reg->pledge_hd, hook_name, &caller);
	NDX_SET_ERR(NDX_OK);
	return NDX_OK;
}

/* -------------------------------------------------------------------------
 * ndx_on_claim — register a claim handler on the caller's current region
 * ------------------------------------------------------------------------- */

int ndx_on_claim(ndx_claim_handler_fn_t *fn, void *ud) {
	ndx_init_once();
	ndx_region_entry_t *reg = region_lookup(ndx_current_region_id);
	if (!reg) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}
	reg->claim_handler    = fn;
	reg->claim_handler_ud = ud;
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

		/* Check that no existing region uses this ID as its map key.
		 * Since region_store keys by ID alone, any ID collision —
		 * regardless of plen — would overwrite an existing entry. */
		int occupied = 0;
		unsigned c = qmap_iter(region_hd, NULL, 0);
		const void *key, *value;
		while (qmap_next(&key, &value, c)) {
			ndx_region_entry_t *e = qmap_ptr(value);
			if (!e) continue;
			if (e->id == candidate_id) {
				occupied = 1;
				break;
			}
		}
		if (!occupied)
			return candidate_id;
		s++;
	} while (s != num_slots);

	return NDX_REGION_INVALID;
}

int ndx_claim(uint8_t bits) {
	ndx_init_once();

	if (bits == 0 || bits > 64) {
		NDX_SET_ERR(NDX_ERR_INVALID);
		return NDX_ERR_INVALID;
	}

	const char *caller = ndx_current_caller;
	uint64_t parent_id = ndx_current_region_id;

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
	region_store(child);

	/* Update caller's region context for the remainder of ndx_install */
	ndx_current_region_id = child_id;

	/* Update the module's ndx_t so all subsequent calls use the child region */
	if (caller) {
		char key[256];
		mod_key(key, sizeof(key), caller, parent_id);
		ndx_mod_entry_t *me = qmap_ptr(qmap_get(mod_hd, key));
		if (me) {
			me->region_id = child_id;
			/* Re-key under new region */
			char new_key[256];
			mod_key(new_key, sizeof(new_key), caller, child_id);
			qmap_put(mod_hd, new_key, &me);
			qmap_del(mod_hd, key);
		}
		/* Update the live ndx_t inside the module */
		void *sl = me ? me->handle : NULL;
		if (sl) {
			get_ndx_func_t get_ndx = NULL;
			*(void **)&get_ndx = dlsym(sl, "get_ndx_ptr");
			if (get_ndx) {
				ndx_t *indx = get_ndx();
				indx->region_id = child_id;
			}
		}
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
	} else {
		node->next = reg->denied_modules;
		reg->denied_modules = node;
	}

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
	node->fn        = fn;
	node->ud        = ud;
	/* Prepend; chain is walked root-first so we reverse at call time */
	node->next = reg->interceptors;
	reg->interceptors = node;

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

/* -------------------------------------------------------------------------
 * Module shutdown helpers
 * ------------------------------------------------------------------------- */

static void ndx_exist(void) {
	ndx_init_once();
	unsigned c = qmap_iter(mod_hd, NULL, 0);
	const void *key, *value;

	while (qmap_next(&key, &value, c)) {
		ndx_mod_entry_t *entry = qmap_ptr(value);
		if (entry) dlclose(entry->handle);
	}
}

/* -------------------------------------------------------------------------
 * Module loading
 * ------------------------------------------------------------------------- */

int _mod_run(void *sl, char *symbol) {
	void (*cb)(void) = NULL;

	* (void **) &cb = dlsym(sl, symbol);

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
	indx->claim            = ndx_claim;
	indx->on_claim         = ndx_on_claim;
	indx->region_each      = ndx_region_each;
}

int _mod_load(char *fname) {
	char *symbol;
	void *sl;

	#ifdef _WIN32
	const char *ext = ".dll";
	#else
	const char *ext = ".so";
	#endif
	size_t flen = strlen(fname);
	size_t elen = strlen(ext);
	char *buf = alloca(flen + elen + 1);
	memcpy(buf, fname, flen);
	memcpy(buf + flen, ext, elen + 1);

	sl = dlopen(buf, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);

	if (!sl) {
		WARN("_mod_load failed loading '%s': %s\n", fname, dlerror());
		return NDX_ERR_NOTFOUND;
	}

	/* Key by (path, region_id) — allows same .so in multiple regions */
	uint64_t inherited_region_id = ndx_current_region_id;
	char mod_key_buf[512];
	mod_key(mod_key_buf, sizeof(mod_key_buf), fname, inherited_region_id);

	const ndx_mod_entry_t *existing = qmap_ptr(qmap_get(mod_hd, mod_key_buf));
	if (existing)
		return NDX_OK;

	symbol = "ndx_install";
	WARN("%s: '%s'\n", symbol, fname);

	/* Duplicate fname so module_path remains valid after the caller's buffer
	 * is freed (e.g. alloca'd in a parent frame). */
	char *stable_fname = strdup(fname);
	/* Duplicate the composite key too */
	size_t klen = mod_key_len(fname);
	char *stable_key = malloc(klen);
	memcpy(stable_key, mod_key_buf, klen);

	/* Register module early so concurrent ndx_loads see the entry. */
	ndx_mod_entry_t *mod_entry = malloc(sizeof(*mod_entry));
	mod_entry->handle    = sl;
	mod_entry->region_id = inherited_region_id;
	qmap_put(mod_hd, stable_key, &mod_entry);

	get_ndx_func_t get_ndx = NULL;
	* (void **) &get_ndx = dlsym(sl, "get_ndx_ptr");
	if (get_ndx) {
		ndx_t *indx = get_ndx();
		_ndx_init(indx, stable_fname, inherited_region_id);
	}

	/* Set thread-local region context for the duration of ndx_install so
	 * any ndx_load() calls inside install inherit the correct region. */
	uint64_t prev_region_id = ndx_current_region_id;
	const char *prev_caller = ndx_current_caller;

	ndx_current_region_id = inherited_region_id;
	ndx_current_caller    = stable_fname;

	int runret = _mod_run(sl, symbol);

	/* Restore context */
	ndx_current_region_id = prev_region_id;
	ndx_current_caller    = prev_caller;

	if (runret != NDX_OK) {
		qmap_del(mod_hd, stable_key);
		free(mod_entry);
		free(stable_fname);
		free(stable_key);
		dlclose(sl);
		return runret;
	}

	return NDX_OK;
}

int ndx_load(char *fname) {
	ndx_init_once();
	int ret = _mod_load(fname);
	NDX_SET_ERR(ret);
	return ret;
}

/* -------------------------------------------------------------------------
 * ndx_last
 * ------------------------------------------------------------------------- */

int ndx_last(void *ret) {
	ndx_init_once();
	if (!ndx.adapter) {
		NDX_SET_ERR(NDX_ERR_INVALID);
		return NDX_ERR_INVALID;
	}
	if (!ndx.adapter->ran) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

	memcpy(ret, ndx.adapter->ret, ndx.adapter->ret_size);
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
	unsigned                 *ran_out;
	void                    **handles;
	int                       handle_count;
} ndx_dispatch_ctx_t;

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

	for (int i = 0; i < ctx->handle_count; i++) {
		void *cb = dlsym(ctx->handles[i], ctx->adapter->name);
		if (!cb) continue;

		get_ndx_func_t get_ndx = NULL;
		* (void **) &get_ndx = dlsym(ctx->handles[i], "get_ndx_ptr");
		if (!get_ndx) continue;
		ndx_t *indx = get_ndx();
		indx->adapter = ctx->adapter;

		uint64_t prev_rid = ndx_current_region_id;
		ndx_current_region_id = indx->region_id;

		ctx->adapter->call(ret, cb, args);
		(*ctx->ran_out)++;
		memcpy(ctx->adapter->ret, ret, ctx->adapter->ret_size);

		ndx_current_region_id = prev_rid;
	}
}

int
ndx_call(void *retp, char *name, void *arg)
{
	uint64_t region_id = ndx_current_region_id;
	ndx_init_once();

	/* ------------------------------------------------------------------
	 * 1. Root-scoped pledge check
	 * ------------------------------------------------------------------ */
	const char *global_pledge_owner = qmap_ptr(qmap_get(pledge_hd, name));
	if (global_pledge_owner) {
		const char *caller = ndx_current_caller;
		if (!caller || strcmp(caller, global_pledge_owner) != 0) {
			WARN("ndx_call: pledge violation — '%s' called '%s' (owner: '%s')\n",
				caller ? caller : "(unknown)", name, global_pledge_owner);
			NDX_SET_ERR(NDX_ERR_EPERM);
			return NDX_ERR_EPERM;
		}
	}

	/* ------------------------------------------------------------------
	 * 2. Collect ancestor chain and check denials / region pledge
	 * ------------------------------------------------------------------ */
	ndx_region_entry_t *anc_chain[65];
	int anc_n = region_ancestor_chain(region_id, anc_chain, 65);
	ndx_region_entry_t *caller_entry = region_lookup(region_id);

	for (int i = 0; i < anc_n; i++) {
		/* Denied hook? Fires for any call into the denying region or its descendants. */
		for (ndx_deny_entry_t *e = anc_chain[i]->denied_hooks; e; e = e->next) {
			if (strcmp(e->value, name) != 0) continue;
			NDX_SET_ERR(NDX_ERR_EPERM);
			return NDX_ERR_EPERM;
		}
		/* Region-scoped pledge check */
		if (anc_chain[i]->pledge_hd) {
			const char *rp_owner =
				qmap_ptr(qmap_get(anc_chain[i]->pledge_hd, name));
			if (rp_owner) {
				const char *caller = ndx_current_caller;
				if (!caller || strcmp(caller, rp_owner) != 0) {
					WARN("ndx_call: pledge violation — '%s' called"
					     " '%s' in region %llu (owner: '%s')\n",
					     caller ? caller : "(unknown)", name,
					     (unsigned long long)anc_chain[i]->id, rp_owner);
					NDX_SET_ERR(NDX_ERR_EPERM);
					return NDX_ERR_EPERM;
				}
			}
		}
	}

	/* ------------------------------------------------------------------
	 * 4. Find the adapter
	 * ------------------------------------------------------------------ */
	ndx_adapter_t adapter;
	const ndx_adapter_t *reg = qmap_ptr(qmap_get(sica_hd, name));

	if (!reg) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

	if (reg->ret_size > NDX_MAX_RET_SIZE) {
		NDX_SET_ERR(NDX_ERR_TOOBIG);
		return NDX_ERR_TOOBIG;
	}

	adapter = *reg;
	adapter.ran = 0;

	/* ------------------------------------------------------------------
	 * 5. Collect eligible module handles
	 * ------------------------------------------------------------------ */
	int handle_count = 0;
	{
		unsigned c = qmap_iter(mod_hd, NULL, 0);
		const void *key, *value;
		while (qmap_next(&key, &value, c)) {
			ndx_mod_entry_t *me = qmap_ptr(value);
			if (!me) continue;
			if (!caller_entry ||
			    !region_is_ancestor(caller_entry, me->region_id))
				continue;
			int denied = 0;
			for (int i = 0; i < anc_n && !denied; i++) {
				/* For denied_modules we match on the module path, which is
				 * the part of the composite key before the NUL byte.
				 * Extract it from the key string. */
				if (deny_list_contains(anc_chain[i]->denied_modules,
				                       (const char *)key))
					denied = 1;
			}
			if (!denied) handle_count++;
		}
	}

	void **handles = handle_count > 0
		? alloca(handle_count * sizeof(void *)) : NULL;
	int hi = 0;
	{
		unsigned c = qmap_iter(mod_hd, NULL, 0);
		const void *key, *value;
		while (qmap_next(&key, &value, c)) {
			ndx_mod_entry_t *me = qmap_ptr(value);
			if (!me) continue;
			if (!caller_entry ||
			    !region_is_ancestor(caller_entry, me->region_id))
				continue;
			int denied = 0;
			for (int i = 0; i < anc_n && !denied; i++)
				if (deny_list_contains(anc_chain[i]->denied_modules,
				                       (const char *)key))
					denied = 1;
			if (!denied) handles[hi++] = me->handle;
		}
	}

	/* ------------------------------------------------------------------
	 * 6. Collect interceptors (root-first)
	 * ------------------------------------------------------------------ */
#define MAX_INTERCEPTORS 256
	ndx_interceptor_entry_t *ic_chain[MAX_INTERCEPTORS];
	int ic_n = 0;

	for (int ai = 0; ai < anc_n && ic_n < MAX_INTERCEPTORS; ai++) {
		ndx_region_entry_t *anc = anc_chain[ai];
		int reg_ic_start = ic_n;
		for (ndx_interceptor_entry_t *ic = anc->interceptors; ic; ic = ic->next) {
			if (strcmp(ic->hook_name, name) == 0 &&
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

	/* ------------------------------------------------------------------
	 * 7. Run dispatch (through interceptor chain)
	 * ------------------------------------------------------------------ */
	ndx_dispatch_ctx_t ctx = {
		.chain        = ic_chain,
		.chain_len    = ic_n,
		.chain_pos    = 0,
		.hook_name    = name,
		.adapter      = &adapter,
		.ran_out      = &adapter.ran,
		.handles      = handles,
		.handle_count = handle_count,
	};

	int pre_err = NDX_GET_ERR();
	dispatch_next(arg, retp, &ctx);

	ndx_last_adapter = adapter;
	ndx.adapter = &ndx_last_adapter;
	if (NDX_GET_ERR() == pre_err)
		NDX_SET_ERR(NDX_OK);
	return NDX_OK;
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
	NDX_SET_ERR(NDX_OK);
	return 0;
}

/* -------------------------------------------------------------------------
 * ndx_shutdown
 * ------------------------------------------------------------------------- */

void
ndx_shutdown(void)
{
#ifndef _WIN32
	ndx_exist();
#endif
	/* Free region entries */
	{
		unsigned c = qmap_iter(region_hd, NULL, 0);
		const void *key, *value;
		while (qmap_next(&key, &value, c)) {
			ndx_region_entry_t *e = qmap_ptr(value);
			region_entry_free(e);
		}
	}
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
	ndx.claim            = ndx_claim;
	ndx.on_claim         = ndx_on_claim;
	ndx.region_each      = ndx_region_each;
}

static void
ndx_init_once(void)
{
	if (ndx_inited)
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

	if (!sica_hd)
		sica_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, SICA_MASK, 0);
	mod_hd    = qmap_open(NULL, NULL, mod_key_type, ndx_ptr_type, MOD_MASK, 0);
	pledge_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, MOD_MASK, 0);
	region_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, REGION_MASK, 0);

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
