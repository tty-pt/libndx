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
unsigned mod_hd,            /* module key -> ndx_mod_entry_t* */
         mod_by_region_hd,  /* region_id (uint64_t) -> ndx_mod_entry_t* (MULTIVALUE) */
         sica_hd,           /* hook name  -> ndx_adapter_t*   */
         pledge_hd,         /* hook name  -> owner path (global/root pledge) */
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
static void ndx_init_once(void);
static int _mod_unload(char *fname, uint64_t region_id);

/* Running count of successfully loaded modules (for alloca sizing in ndx_call) */
static int ndx_mod_count;

/* Running count of registered pledges (root + region-scoped combined).
 * Used to skip the pledge_hd hash lookup entirely when no pledges exist. */
static int ndx_pledge_count;

/* Last-used adapter state, stored persistently so ndx.adapter doesn't dangle */
static ndx_adapter_t ndx_last_adapter;
/* Pointer to the registered adapter used in the last call.
 * When reg == ndx_last_reg the metadata fields of ndx_last_adapter are already
 * correct — we skip the 88-byte memcpy and only reset ran to 0. */
static const ndx_adapter_t *ndx_last_reg;

/* Thread-local caller identity (for pledge enforcement) */
static __thread const char *ndx_current_caller;

/* Thread-local current region — id and a direct pointer kept in sync.
 * ndx_current_region may be NULL before the first ndx_init_once() call;
 * code that needs the entry must guard or call region_ensure_root() first. */
static __thread uint64_t           ndx_current_region_id = NDX_REGION_ROOT;
static __thread ndx_region_entry_t *ndx_current_region    = NULL;

/* Thread-local pointer to the currently-loading module entry.
 * Set during ndx_install() so child ndx_load() calls can register their
 * parent_entry for cascade-unload tracking. */
static __thread ndx_mod_entry_t *ndx_loading_mod = NULL;

/* Keep the id/pointer pair in sync atomically (within a thread). */
static inline void
set_current_region(uint64_t id, ndx_region_entry_t *entry)
{
	ndx_current_region_id = id;
	ndx_current_region    = entry;
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

/* Check if hook_name is in a deny list */
static int
deny_list_contains(ndx_deny_entry_t *head, const char *value)
{
	for (ndx_deny_entry_t *e = head; e; e = e->next)
		if (strcmp(e->value, value) == 0) return 1;
	return 0;
}

/* Propagate a subtree flag upward from entry to root. */
static inline void
region_propagate_deny(ndx_region_entry_t *entry)
{
	for (ndx_region_entry_t *e = entry; e; e = e->parent)
		e->subtree_has_deny = 1;
}

static inline void
region_propagate_pledge(ndx_region_entry_t *entry)
{
	for (ndx_region_entry_t *e = entry; e; e = e->parent)
		e->subtree_has_pledge = 1;
}

static inline void
region_propagate_interceptors(ndx_region_entry_t *entry)
{
	for (ndx_region_entry_t *e = entry; e; e = e->parent)
		e->subtree_has_interceptors = 1;
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
	if (!ndx_current_region || ndx_current_region_id == NDX_REGION_ROOT)
		ndx_current_region = root;
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
	if (qmap_get(reg->pledge_hd, hook_name)) {
		NDX_SET_ERR(NDX_ERR_EPERM);
		return NDX_ERR_EPERM;
	}
	qmap_put(reg->pledge_hd, hook_name, &caller);
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

	/* Update the thread-local region context so ndx_install sees child */
	set_current_region(child_id, child);

	/* Update the mod entry and re-key it under the new region */
	if (caller) {
		char key[256];
		mod_key(key, sizeof(key), caller, parent_id);
		ndx_mod_entry_t *me = qmap_ptr(qmap_get(mod_hd, key));
		if (me) {
			me->region_id = child_id;
			char new_key[256];
			mod_key(new_key, sizeof(new_key), caller, child_id);
			qmap_put(mod_hd, new_key, &me);
			qmap_del(mod_hd, key);

			/* Re-key in secondary index:
			 * mod_by_region_hd is QM_MULTIVALUE; collect all entries for
			 * parent_id, delete them all, re-insert all except me under
			 * parent_id, then insert me under child_id. */
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
		/* Update the live ndx_t inside the module — already cached in me->indx */
		if (me && me->indx)
			me->indx->region_id = child_id;
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
	node->fn        = fn;
	node->ud        = ud;
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

/* -------------------------------------------------------------------------
 * Module shutdown helpers
 * ------------------------------------------------------------------------- */

static void ndx_exist(void) {
	ndx_init_once();
	unsigned c = qmap_iter(mod_hd, NULL, 0);
	const void *key, *value;

	while (qmap_next(&key, &value, c)) {
		ndx_mod_entry_t *entry = qmap_ptr(value);
		if (entry) {
			free(entry->fn_cache);
			dlclose(entry->handle);
		}
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
	indx->require_claim    = ndx_require_claim;
	indx->region_each      = ndx_region_each;
	indx->unload           = ndx_unload;
	indx->reload           = ndx_reload;
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

	ndx_mod_entry_t *existing = qmap_ptr(qmap_get(mod_hd, mod_key_buf));
	if (existing) {
		existing->refcount++;
		dlclose(sl); /* balance the extra dlopen; RTLD_NODELETE keeps it loaded */
		return NDX_OK;
	}

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
	ndx_mod_entry_t *mod_entry = calloc(1, sizeof(*mod_entry));
	mod_entry->handle       = sl;
	mod_entry->region_id    = inherited_region_id;
	mod_entry->indx         = NULL;
	mod_entry->refcount     = 1;
	mod_entry->parent_entry = ndx_loading_mod;
	mod_entry->mod_key      = stable_key;
	qmap_put(mod_hd, stable_key, &mod_entry);

	get_ndx_func_t get_ndx = NULL;
	* (void **) &get_ndx = dlsym(sl, "get_ndx_ptr");
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
	uint8_t *claim_sym = (uint8_t *)dlsym(sl, "ndx_claim");

	if (inherited_reg && inherited_reg->require_claim && !claim_sym) {
		/* No ndx_claim symbol — reject */
		qmap_del(mod_by_region_hd, &mod_entry->region_id);
		qmap_del(mod_hd, stable_key);
		free(mod_entry->fn_cache);
		free(mod_entry->mod_key);
		free(mod_entry);
		free(stable_fname);
		dlclose(sl);
		NDX_SET_ERR(NDX_ERR_EPERM);
		return NDX_ERR_EPERM;
	}

	/* Set thread-local region context for the duration of ndx_install so
	 * any ndx_load() calls inside install inherit the correct region. */
	uint64_t            prev_region_id    = ndx_current_region_id;
	ndx_region_entry_t *prev_region_entry = ndx_current_region;
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
			qmap_del(mod_by_region_hd, &mod_entry->region_id);
			qmap_del(mod_hd, stable_key);
			free(mod_entry->fn_cache);
			free(mod_entry->mod_key);
			free(mod_entry);
			free(stable_fname);
			dlclose(sl);
			return cret;
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
		qmap_del(mod_by_region_hd, &mod_entry->region_id);
		qmap_del(mod_hd, stable_key);
		free(mod_entry->fn_cache);
		free(mod_entry->mod_key);
		free(mod_entry);
		free(stable_fname);
		dlclose(sl);
		return runret;
	}

	ndx_mod_count++;

	/* Allocate per-region state if the module exports ndx_region_state_size */
	{
		typedef size_t ndx_region_state_size_t(void);
		ndx_region_state_size_t *sz_fn = NULL;
		*(void **)&sz_fn = dlsym(sl, "ndx_region_state_size");
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
			mod_entry->region_entry = re;
			mod_entry->region_next  = NULL;
			mod_entry->region_prev  = re->mods_tail;
			if (re->mods_tail)
				re->mods_tail->region_next = mod_entry;
			else
				re->mods_head = mod_entry;
			re->mods_tail = mod_entry;
		}
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
	char mod_key_buf[512];
	mod_key(mod_key_buf, sizeof(mod_key_buf), fname, region_id);

	ndx_mod_entry_t *entry = qmap_ptr(qmap_get(mod_hd, mod_key_buf));
	if (!entry) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

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
			/* Build the fname from module_path (stored in indx) */
			const char *child_path = child->indx ? child->indx->module_path : NULL;
			if (child_path)
				_mod_unload((char *)child_path, child->region_id);
		}
		free(children);
	}

	/* Invalidate fn_cache entries across all other modules that may have
	 * cached a function pointer from the about-to-be-unloaded .so.
	 * Strategy: use dladdr to check whether each cached pointer belongs to
	 * the unloaded handle.  If dladdr returns info with dli_fbase matching
	 * our handle's base, the entry is stale. */
	{
#ifndef _WIN32
		/* Get the base address of the unloading library */
		void *any_sym = dlsym(entry->handle, "ndx_install");
		if (!any_sym) any_sym = dlsym(entry->handle, "get_ndx_ptr");
		void *base = NULL;
		if (any_sym) {
			Dl_info di;
			if (dladdr(any_sym, &di))
				base = di.dli_fbase;
		}
		if (base) {
			unsigned c = qmap_iter(mod_hd, NULL, 0);
			const void *key, *value;
			while (qmap_next(&key, &value, c)) {
				ndx_mod_entry_t *m = qmap_ptr(value);
				if (!m || m == entry) continue;
				for (int i = 0; i < m->fn_cache_cap; i++) {
					void *fp = m->fn_cache[i];
					if (!fp || fp == NDX_FN_NOT_FOUND) continue;
					Dl_info fi;
					if (dladdr(fp, &fi) && fi.dli_fbase == base)
						m->fn_cache[i] = NULL; /* force re-resolve */
				}
			}
		}
#endif
	}

	/* Remove deny entries for this module's path */
	{
		ndx_region_entry_t *re = entry->region_entry;
		if (re) {
			const char *path = entry->indx ? entry->indx->module_path : NULL;
			if (path) {
				/* denied_modules list */
				ndx_deny_entry_t **pp = &re->denied_modules;
				while (*pp) {
					if (strcmp((*pp)->value, path) == 0) {
						ndx_deny_entry_t *dead = *pp;
						*pp = dead->next;
						free(dead->value);
						free(dead);
					} else {
						pp = &(*pp)->next;
					}
				}
				/* interceptors registered by this module */
				ndx_interceptor_entry_t **ip = &re->interceptors;
				while (*ip) {
					/* We identify interceptors by the module's fn_cache
					 * address range — use the same dladdr base trick. */
#ifndef _WIN32
					void *base2 = NULL;
					void *any2 = dlsym(entry->handle, "ndx_install");
					if (!any2) any2 = dlsym(entry->handle, "get_ndx_ptr");
					if (any2) {
						Dl_info di2;
						if (dladdr(any2, &di2)) base2 = di2.dli_fbase;
					}
					if (base2) {
						Dl_info fi2;
						void *fn_ptr;
						memcpy(&fn_ptr, &(*ip)->fn, sizeof(fn_ptr));
						if (dladdr(fn_ptr, &fi2) &&
						    fi2.dli_fbase == base2) {
							ndx_interceptor_entry_t *dead = *ip;
							*ip = dead->next;
							free(dead->hook_name);
							free(dead);
							continue;
						}
					}
#endif
					ip = &(*ip)->next;
				}
				/* pledges owned by this module in region-scoped pledge_hd */
				if (re->pledge_hd) {
					unsigned pc = qmap_iter(re->pledge_hd, NULL, 0);
					const void *pk, *pv;
					/* collect keys to delete */
					const char **del_keys = NULL;
					int ndel = 0, del_cap = 0;
					while (qmap_next(&pk, &pv, pc)) {
						const char *owner = qmap_ptr(pv);
						if (owner && strcmp(owner, path) == 0) {
							if (ndel >= del_cap) {
								del_cap = del_cap ? del_cap*2 : 4;
								del_keys = realloc(del_keys,
								           del_cap * sizeof(char*));
							}
							del_keys[ndel++] = (const char *)pk;
						}
					}
					for (int i = 0; i < ndel; i++) {
						qmap_del(re->pledge_hd, del_keys[i]);
						ndx_pledge_count--;
					}
					free(del_keys);
				}
				/* root pledge map */
				{
					unsigned pc = qmap_iter(pledge_hd, NULL, 0);
					const void *pk, *pv;
					const char **del_keys = NULL;
					int ndel = 0, del_cap = 0;
					while (qmap_next(&pk, &pv, pc)) {
						const char *owner = qmap_ptr(pv);
						if (owner && strcmp(owner, path) == 0) {
							if (ndel >= del_cap) {
								del_cap = del_cap ? del_cap*2 : 4;
								del_keys = realloc(del_keys,
								           del_cap * sizeof(char*));
							}
							del_keys[ndel++] = (const char *)pk;
						}
					}
					for (int i = 0; i < ndel; i++) {
						qmap_del(pledge_hd, del_keys[i]);
						ndx_pledge_count--;
					}
					free(del_keys);
				}
			}
		}
	}

	/* Remove from region's doubly-linked list (O(1)) */
	{
		ndx_region_entry_t *re = entry->region_entry;
		if (re) {
			if (entry->region_prev)
				entry->region_prev->region_next = entry->region_next;
			else
				re->mods_head = entry->region_next;

			if (entry->region_next)
				entry->region_next->region_prev = entry->region_prev;
			else
				re->mods_tail = entry->region_prev;
		}
	}

	/* Remove from hash maps */
	qmap_del(mod_by_region_hd, &entry->region_id);
	qmap_del(mod_hd, entry->mod_key);

	ndx_mod_count--;

	/* Release resources */
	void *handle = entry->handle;
	const char *module_path = entry->indx ? entry->indx->module_path : NULL;

	/* Per-region state cleanup */
	if (entry->region_state) {
		typedef void ndx_region_cleanup_t(void *state);
		ndx_region_cleanup_t *cleanup_fn = NULL;
		*(void **)&cleanup_fn = dlsym(entry->handle, "ndx_region_cleanup");
		if (cleanup_fn)
			cleanup_fn(entry->region_state);
		free(entry->region_state);
	}

	free(entry->fn_cache);
	free(entry->mod_key);
	if (module_path) free((char *)module_path);
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
	char mod_key_buf[512];
	mod_key(mod_key_buf, sizeof(mod_key_buf), fname, region_id);
	ndx_mod_entry_t *existing = qmap_ptr(qmap_get(mod_hd, mod_key_buf));
	if (!existing) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

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
	ndx_mod_entry_t *new_entry = qmap_ptr(qmap_get(mod_hd, mod_key_buf));
	if (!new_entry || !re) {
		NDX_SET_ERR(NDX_OK);
		return NDX_OK;
	}

	/* Splice the new entry out of tail position and re-insert at saved position */
	/* Remove from tail */
	if (new_entry->region_prev)
		new_entry->region_prev->region_next = new_entry->region_next;
	else
		re->mods_head = new_entry->region_next;
	if (new_entry->region_next)
		new_entry->region_next->region_prev = new_entry->region_prev;
	else
		re->mods_tail = new_entry->region_prev;

	/* Insert after insert_after (or at head if insert_after == NULL) */
	if (insert_after == NULL) {
		/* Place at head */
		new_entry->region_prev = NULL;
		new_entry->region_next = re->mods_head;
		if (re->mods_head)
			re->mods_head->region_prev = new_entry;
		else
			re->mods_tail = new_entry;
		re->mods_head = new_entry;
	} else {
		/* Insert after insert_after */
		new_entry->region_prev = insert_after;
		new_entry->region_next = insert_after->region_next;
		if (insert_after->region_next)
			insert_after->region_next->region_prev = new_entry;
		else
			re->mods_tail = new_entry;
		insert_after->region_next = new_entry;
	}

	NDX_SET_ERR(NDX_OK);
	return NDX_OK;
}

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
	ndx_mod_entry_t         **entries;
	int                       entry_count;
	int                       hook_id;   /* pre-resolved at ndx_call time */
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

	for (int i = 0; i < ctx->entry_count; i++) {
		ndx_mod_entry_t *me = ctx->entries[i];

		/* Resolve function pointer via per-module cache (avoid dlsym per call).
		 * hook_id was pre-resolved in ndx_call — no hash lookup here. */
		int hook_id = ctx->hook_id;
		void *cb = NULL;
		if (__builtin_expect(hook_id >= 0, 1)) {
			/* Grow cache if needed */
			if (hook_id >= me->fn_cache_cap) {
				int new_cap = hook_id + 16;
				me->fn_cache = realloc(me->fn_cache, new_cap * sizeof(void *));
				/* Initialise new slots to NULL (= uncached) */
				memset(me->fn_cache + me->fn_cache_cap, 0,
				       (new_cap - me->fn_cache_cap) * sizeof(void *));
				me->fn_cache_cap = new_cap;
			}
			if (me->fn_cache[hook_id] == NULL) {
				void *sym = dlsym(me->handle, ctx->adapter->name);
				me->fn_cache[hook_id] = sym ? sym : NDX_FN_NOT_FOUND;
			}
			cb = (me->fn_cache[hook_id] == NDX_FN_NOT_FOUND)
			     ? NULL : me->fn_cache[hook_id];
		} else {
			/* Hook ID unknown (registered after module load?) — fall back */
			cb = dlsym(me->handle, ctx->adapter->name);
		}

		if (!cb) continue;

		/* Use cached ndx_t* and region_entry — no hash lookups needed */
		ndx_t *indx = me->indx;
		if (!indx) continue;
		indx->adapter = ctx->adapter;
		indx->region_state = me->region_state;

		uint64_t region_changed = (indx->region_id != ndx_current_region_id);
		uint64_t            prev_rid    = ndx_current_region_id;
		ndx_region_entry_t *prev_rentry = ndx_current_region;
		if (__builtin_expect(region_changed, 0))
			set_current_region(indx->region_id, me->region_entry);

		ctx->adapter->call(ret, cb, args);
		(*ctx->ran_out)++;
		memcpy(ctx->adapter->ret, ret, ctx->adapter->ret_size);

		if (__builtin_expect(region_changed, 0))
			set_current_region(prev_rid, prev_rentry);
	}
}

int
ndx_call(void *retp, ndx_adapter_t *reg, void *arg, const char *caller)
{
	uint64_t region_id = ndx_current_region_id;
	if (__builtin_expect(!ndx_inited, 0))
		ndx_init_once();

	if (!reg) {
		NDX_SET_ERR(NDX_ERR_NOTFOUND);
		return NDX_ERR_NOTFOUND;
	}

	/* ------------------------------------------------------------------
	 * 1. Root-scoped pledge check (skipped when no pledges registered).
	 *    Caller identity is only written to TLS when actually needed.
	 * ------------------------------------------------------------------ */
	if (__builtin_expect(ndx_pledge_count > 0, 0)) {
		ndx_current_caller = caller;
		const char *global_pledge_owner = qmap_ptr(qmap_get(pledge_hd, reg->name));
		if (global_pledge_owner) {
			const char *caller = ndx_current_caller;
			if (!caller || strcmp(caller, global_pledge_owner) != 0) {
				WARN("ndx_call: pledge violation — '%s' called '%s' (owner: '%s')\n",
					caller ? caller : "(unknown)", reg->name, global_pledge_owner);
				NDX_SET_ERR(NDX_ERR_EPERM);
				return NDX_ERR_EPERM;
			}
		}
	}

	/* ------------------------------------------------------------------
	 * 2. Security pre-check using cached subtree flags.
	 *
	 * Each ndx_region_entry_t carries subtree_has_* flags that are set
	 * whenever a deny/pledge/interceptor is registered anywhere in the
	 * subtree rooted at that entry.  Reading three bytes off the caller's
	 * cached region pointer (ndx_current_region) tells us whether we need
	 * to do any further work — no ancestor walk required on the clean path.
	 * ------------------------------------------------------------------ */
	ndx_region_entry_t *caller_region_entry = ndx_current_region;
	if (__builtin_expect(!caller_region_entry, 0))
		caller_region_entry = region_lookup(region_id);

	int has_deny_pledge   = caller_region_entry &&
	                        (caller_region_entry->subtree_has_deny ||
	                         caller_region_entry->subtree_has_pledge);
	int has_interceptors  = caller_region_entry &&
	                         caller_region_entry->subtree_has_interceptors;

	/* Ancestor chain — built lazily, only when security checks are needed */
	ndx_region_entry_t *anc_chain[65];
	int anc_n = 0;

	if (__builtin_expect(has_deny_pledge | has_interceptors, 0)) {
		if (caller_region_entry)
			anc_n = region_ancestor_chain(caller_region_entry, anc_chain, 65);
	}

	if (__builtin_expect(has_deny_pledge, 0)) {
		/* Caller identity needed for pledge checks in this branch */
		ndx_current_caller = caller;
		for (int i = 0; i < anc_n; i++) {
			/* Denied hook? Fires for any call into the denying region or its descendants. */
			for (ndx_deny_entry_t *e = anc_chain[i]->denied_hooks; e; e = e->next) {
				if (strcmp(e->value, reg->name) != 0) continue;
				NDX_SET_ERR(NDX_ERR_EPERM);
				return NDX_ERR_EPERM;
			}
			/* Region-scoped pledge check */
			if (anc_chain[i]->pledge_hd) {
				const char *rp_owner =
					qmap_ptr(qmap_get(anc_chain[i]->pledge_hd, reg->name));
				if (rp_owner) {
					if (!caller || strcmp(caller, rp_owner) != 0) {
						WARN("ndx_call: pledge violation — '%s' called"
						     " '%s' in region %llu (owner: '%s')\n",
						     caller ? caller : "(unknown)", reg->name,
						     (unsigned long long)anc_chain[i]->id, rp_owner);
						NDX_SET_ERR(NDX_ERR_EPERM);
						return NDX_ERR_EPERM;
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
	if (__builtin_expect(hook_id < 0, 0)) {
		const void *hid_v = qmap_get(hook_id_hd, reg->name);
		if (!hid_v) {
			NDX_SET_ERR(NDX_ERR_NOTFOUND);
			return NDX_ERR_NOTFOUND;
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
	}

	if (reg->ret_size > NDX_MAX_RET_SIZE) {
		NDX_SET_ERR(NDX_ERR_TOOBIG);
		return NDX_ERR_TOOBIG;
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
	int pre_err = NDX_GET_ERR();

	if (__builtin_expect(!has_interceptors, 1)) {
		/* ---- Fast path: inline DFS dispatch (no interceptors) ---- */
		unsigned ran = 0;

		/* Pre-populate ndx_last_adapter metadata so ndx.adapter is valid
		 * during module execution (modules may call ndx.last() mid-dispatch).
		 * Skip the 88-byte metadata copy when the hook hasn't changed since
		 * the last call — reg pointer identity suffices as a cache key. */
		if (__builtin_expect(reg != ndx_last_reg, 0)) {
			memcpy(&ndx_last_adapter, reg, offsetof(ndx_adapter_t, ret));
			ndx_last_reg = reg;
		}
		ndx_last_adapter.ran = 0;
		ndx.adapter = &ndx_last_adapter;

		if (caller_region_entry) {
			ndx_region_entry_t *stk[65];
			int stk_top = 0;
			stk[stk_top++] = caller_region_entry;

			while (stk_top > 0) {
				ndx_region_entry_t *rn = stk[--stk_top];

				for (ndx_mod_entry_t *me = (ndx_mod_entry_t *)rn->mods_head;
				     me; me = me->region_next) {
					if (!me->indx) continue;

					/* Module-denied check (only when subtree has denies) */
					if (__builtin_expect(has_deny_pledge, 0)) {
						int denied = 0;
						const char *mpath = me->indx->module_path;
						for (int i = 0; i < anc_n && !denied; i++)
							if (deny_list_contains(anc_chain[i]->denied_modules, mpath))
								denied = 1;
						if (denied) continue;
					}

					/* Resolve function pointer via fn_cache */
					void *cb = NULL;
					if (__builtin_expect(hook_id >= 0, 1)) {
						if (hook_id >= me->fn_cache_cap) {
							int new_cap = hook_id + 16;
							me->fn_cache = realloc(me->fn_cache, new_cap * sizeof(void *));
							memset(me->fn_cache + me->fn_cache_cap, 0,
							       (new_cap - me->fn_cache_cap) * sizeof(void *));
							me->fn_cache_cap = new_cap;
						}
						if (me->fn_cache[hook_id] == NULL) {
							void *sym = dlsym(me->handle, reg->name);
							me->fn_cache[hook_id] = sym ? sym : NDX_FN_NOT_FOUND;
						}
						cb = (me->fn_cache[hook_id] == NDX_FN_NOT_FOUND)
						     ? NULL : me->fn_cache[hook_id];
					} else {
						cb = dlsym(me->handle, reg->name);
					}

				if (!cb) continue;

				ndx_t *indx = me->indx;
				indx->adapter = &ndx_last_adapter;
				indx->region_state = me->region_state;

				uint64_t region_changed = (indx->region_id != ndx_current_region_id);
				uint64_t            prev_rid    = ndx_current_region_id;
				ndx_region_entry_t *prev_rentry = ndx_current_region;
				if (__builtin_expect(region_changed, 0))
					set_current_region(indx->region_id, me->region_entry);

				reg->call(retp, cb, arg);
				ran++;

				if (__builtin_expect(region_changed, 0))
					set_current_region(prev_rid, prev_rentry);
			}

			for (ndx_region_entry_t *ch = (ndx_region_entry_t *)rn->children_head;
			     ch && stk_top < 65; ch = (ndx_region_entry_t *)ch->sibling_next)
				stk[stk_top++] = ch;
		}
		}

		/* Commit ran and final ret value to ndx_last_adapter (metadata already set above) */
		ndx_last_adapter.ran = ran;
		if (ran)
			memcpy(ndx_last_adapter.ret, retp, reg->ret_size);

	} else {
		/* ---- Slow path: collect entries[], build interceptor chain ---- */
		int total_mods = ndx_mod_count;
		ndx_mod_entry_t **entries = total_mods > 0
			? alloca(total_mods * sizeof(ndx_mod_entry_t *)) : NULL;
		int entry_count = 0;

		if (caller_region_entry) {
			ndx_region_entry_t *stk[65];
			int stk_top = 0;
			stk[stk_top++] = caller_region_entry;

			while (stk_top > 0) {
				ndx_region_entry_t *rn = stk[--stk_top];

				for (ndx_mod_entry_t *me = (ndx_mod_entry_t *)rn->mods_head;
				     me; me = me->region_next) {
					if (!me->indx) continue;
					int denied = 0;
					const char *mpath = me->indx->module_path;
					for (int i = 0; i < anc_n && !denied; i++)
						if (deny_list_contains(anc_chain[i]->denied_modules, mpath))
							denied = 1;
					if (!denied && entry_count < total_mods)
						entries[entry_count++] = me;
				}

				for (ndx_region_entry_t *ch = (ndx_region_entry_t *)rn->children_head;
				     ch && stk_top < 65; ch = (ndx_region_entry_t *)ch->sibling_next)
					stk[stk_top++] = ch;
			}
		}

#define MAX_INTERCEPTORS 256
		ndx_interceptor_entry_t *ic_chain[MAX_INTERCEPTORS];
		int ic_n = 0;

		for (int ai = 0; ai < anc_n && ic_n < MAX_INTERCEPTORS; ai++) {
			ndx_region_entry_t *anc = anc_chain[ai];
			int reg_ic_start = ic_n;
			for (ndx_interceptor_entry_t *ic = anc->interceptors; ic; ic = ic->next) {
				if (strcmp(ic->hook_name, reg->name) == 0 &&
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

		ndx_dispatch_ctx_t ctx = {
			.chain        = ic_chain,
			.chain_len    = ic_n,
			.chain_pos    = 0,
			.hook_name    = reg->name,
			.adapter      = &adapter,
			.ran_out      = &adapter.ran,
			.entries      = entries,
			.entry_count  = entry_count,
			.hook_id      = hook_id,
		};

		dispatch_next(arg, retp, &ctx);

		/* Commit to ndx_last_adapter — only copy metadata if hook changed */
		if (__builtin_expect(reg != ndx_last_reg, 0)) {
			memcpy(&ndx_last_adapter, reg, offsetof(ndx_adapter_t, ret));
			ndx_last_reg = reg;
		}
		ndx_last_adapter.ran = adapter.ran;
		memcpy(ndx_last_adapter.ret, adapter.ret, reg->ret_size);
	}

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
	/* Assign a monotonic hook ID for the fn_cache index */
	int hook_id = ndx_hook_id_counter++;
	qmap_put(hook_id_hd, name, &hook_id);
	/* Write the ID back into the adapter so callers can skip the hash lookup */
	adapter->hook_id = hook_id;
	/* Grow adapter-by-id array and store pointer */
	if (hook_id >= ndx_adapter_by_id_cap) {
		int new_cap = hook_id + 16;
		ndx_adapter_by_id = realloc(ndx_adapter_by_id,
		                            new_cap * sizeof(ndx_adapter_t *));
		memset(ndx_adapter_by_id + ndx_adapter_by_id_cap, 0,
		       (new_cap - ndx_adapter_by_id_cap) * sizeof(ndx_adapter_t *));
		ndx_adapter_by_id_cap = new_cap;
	}
	ndx_adapter_by_id[hook_id] = adapter;
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
	qmap_close(mod_by_region_hd);
	mod_by_region_hd = 0;
	/* Reset thread-local region pointer — entries were freed above */
	ndx_current_region    = NULL;
	ndx_current_region_id = NDX_REGION_ROOT;
	/* Invalidate the adapter cache — registered adapters may be re-created */
	ndx_last_reg = NULL;
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
	ndx.unload           = ndx_unload;
	ndx.reload           = ndx_reload;
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

	if (!region_id_type)
		region_id_type = qmap_reg(sizeof(uint64_t));

	if (!ndx_int_type)
		ndx_int_type = qmap_reg(sizeof(int));

	if (!sica_hd)
		sica_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, SICA_MASK, 0);
	if (!hook_id_hd)
		hook_id_hd = qmap_open(NULL, NULL, QM_STR, ndx_int_type, SICA_MASK, 0);
	mod_hd           = qmap_open(NULL, NULL, mod_key_type, ndx_ptr_type, MOD_MASK, 0);
	mod_by_region_hd = qmap_open(NULL, NULL, region_id_type, ndx_ptr_type,
	                             MOD_MASK, QM_SORTED | QM_MULTIVALUE);
	pledge_hd = qmap_open(NULL, NULL, QM_STR, ndx_ptr_type, MOD_MASK, 0);
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
