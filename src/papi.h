#ifndef NDX_PAPI_H
#define NDX_PAPI_H

#include "../include/ttypt/ndx.h"
#include <stdint.h>

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

	ndx_claim_handler_fn_t   *claim_handler;
	void                     *claim_handler_ud;
} ndx_region_entry_t;

/*
 * Per-module metadata stored in mod_hd.
 * Keyed by "path\0<region_hex>" to allow same .so in multiple regions.
 */
typedef struct {
	void    *handle;
	uint64_t region_id;
} ndx_mod_entry_t;

/* -------------------------------------------------------------------------
 * Internal ndx_t (host-side function table, injected into each module)
 *
 * Field order through region_id must stay identical to struct ndx_ctx in
 * ndx.h — modules are compiled against ndx.h but the host fills ndx_t.
 * ------------------------------------------------------------------------- */

typedef struct {
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
	ndx_claim_t              *claim;
	ndx_on_claim_t           *on_claim;
	ndx_region_each_t        *region_each;
} ndx_t;

extern ndx_t ndx;

#endif
