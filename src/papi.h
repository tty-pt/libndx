#ifndef NDX_PAPI_H
#define NDX_PAPI_H

#include "../include/ttypt/ndx.h"
#include <stdint.h>

/* Forward declaration — full definition follows below */
typedef struct ndx_t_s ndx_t;

/* -------------------------------------------------------------------------
 * Internal region structures
 * ------------------------------------------------------------------------- */

/* A singly-linked list node for denied hook names or module paths */
typedef struct ndx_deny_entry {
	char *value;                  /* hook name or module path (owned) */
	struct ndx_deny_entry *next;
} ndx_deny_entry_t;

/*
 * Interceptor entry: middleware-pattern hook interceptor registered on a
 * region for a specific hook name.
 */
typedef struct ndx_interceptor_entry {
	char                    *hook_name; /* owned */
	ndx_interceptor_fn_t    *fn;
	void                    *ud;
	struct ndx_interceptor_entry *next;
} ndx_interceptor_entry_t;

/*
 * Internal region descriptor.
 *
 * Region IDs are plain opaque uint64_t prefix values — all 64 bits are
 * available as address space.  The prefix length (plen) is stored here in
 * the entry, not packed into the ID.
 *
 * Ancestry check: mask child_id to ancestor->plen significant bits and
 * compare against ancestor->id.  O(1), no child lookup required.
 *
 * The root region has id=NDX_REGION_ROOT (0) and plen=0; it is an ancestor
 * of everything.
 *
 * depth: number of edges from root to this node (incremented by 1 per claim,
 * regardless of bit width).
 *
 * plen: cumulative prefix length in bits (parent->plen + granted_bits).
 *
 * child_bits: the width granted when this region was claimed.  Used to
 * identify immediate children in ndx_region_each.
 */
typedef struct ndx_region_entry {
	uint64_t                  id;
	uint8_t                   depth;
	uint8_t                   plen;        /* cumulative prefix length in bits */
	uint8_t                   child_bits;  /* width granted at claim time */
	const char               *owner_path;  /* module that owns this region */

	ndx_deny_entry_t         *denied_hooks;   /* hook names blocked here */
	ndx_deny_entry_t         *denied_modules; /* module paths blocked here */
	ndx_interceptor_entry_t  *interceptors;   /* middleware chain (head) */

	unsigned                  pledge_hd;      /* per-region qmap: hook->owner */

	/* Cached flags — set whenever the corresponding field becomes non-NULL/non-zero.
	 * Propagated upward through the ancestor chain so ndx_call can skip the
	 * region_ancestor_chain walk entirely when the subtree is clean. */
	uint8_t                   subtree_has_deny;        /* any deny_hooks/deny_modules in subtree */
	uint8_t                   subtree_has_pledge;      /* any pledge_hd in subtree */
	uint8_t                   subtree_has_interceptors;/* any interceptors in subtree */

	ndx_claim_handler_fn_t   *claim_handler;
	void                     *claim_handler_ud;
	uint8_t                   require_claim;  /* if set, ndx_load rejects modules without ndx_claim symbol */

	struct ndx_region_entry  *parent;         /* direct parent; NULL for root */

	/* Intrusive list of immediate child regions (prepend on claim) */
	struct ndx_region_entry  *children_head;
	struct ndx_region_entry  *sibling_next;

	/* Intrusive list of modules assigned to this region (append on load, preserves order) */
	struct ndx_mod_entry     *mods_head;
	struct ndx_mod_entry     *mods_tail;
} ndx_region_entry_t;

/*
 * Per-module metadata stored in mod_hd.
 * Keyed by "path\0<region_hex>" to allow same .so in multiple regions.
 */
typedef struct ndx_mod_entry {
    void    *handle;
    uint64_t region_id;
    ndx_t   *indx;
    /* Direct pointer to the owning region entry — avoids hash lookup in dispatch. */
    struct ndx_region_entry *region_entry;
    /* Per-hook function-pointer cache.
     * fn_cache[hook_id] == NULL  → not yet resolved (lazy dlsym on first call).
     * fn_cache[hook_id] == NDX_FN_NOT_FOUND → dlsym returned NULL for this hook.
     * Otherwise → the cached function pointer. */
    void   **fn_cache;
    int      fn_cache_cap;
    /* Intrusive list link within the owning region's mods_head list */
    struct ndx_mod_entry *region_next;
} ndx_mod_entry_t;

/* -------------------------------------------------------------------------
 * Internal ndx_t (host-side function table, injected into each module)
 *
 * Field order through region_id must stay identical to struct ndx_ctx in
 * ndx.h — modules are compiled against ndx.h but the host fills ndx_t.
 * ------------------------------------------------------------------------- */

typedef struct ndx_t_s {
	ndx_call_t               *call;
	ndx_areg_t               *areg;
	ndx_load_t               *load;
	ndx_errno_t              *err;
	ndx_strerror_t           *strerror;
	ndx_adapter_t            *adapter;
	ndx_last_t               *last;
	ndx_shutdown_t           *shutdown;
	const char               *module_path;
	ndx_pledge_t             *pledge;
	ndx_set_caller_t         *set_caller;
	uint64_t                  region_id;
	/* region management API */
	ndx_deny_t               *deny;
	ndx_intercept_t          *intercept;
	ndx_require_claim_t      *require_claim;
	ndx_region_each_t        *region_each;
} ndx_t;

extern ndx_t ndx;

#endif
