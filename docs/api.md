# libndx Public Interface

## Principles

1. **Retro-compatibility**: code that never uses regions continues to work unchanged.
   `NDX_DEF`, `NDX_DECL`, `NDX_CALL`, `ndx_load`, `ndx_pledge`, `ndx_shutdown`,
   `ndx_errno`, `ndx_strerror` all keep their pre-region signatures.

2. **Region IDs are internal**. No public function takes or returns a raw region ID.
   `ndx_my_region()` is kept as a diagnostic-only escape hatch but is not part of
   the primary API narrative.

3. **All region management is implicit** — operations always apply to the caller's
   current region, which is set automatically by the dispatch and load machinery.

4. **Modules opt into being region modules** by exporting
   `MODULE_API uint8_t ndx_claim = N` as a data symbol (where N is the
   requested bit width).  If they don't, they live flat in whatever region
   they were loaded into, exactly as before.

 5. **Parents control subdivision** by installing a claim handler via
   `ndx_require_claim(fn, ud)`.  When a module with an `ndx_claim` symbol is
   loaded into a region that has a handler, the host performs the claim
   automatically before running `ndx_install()`.

 6. **Regions can enforce the claim contract** with `ndx_require_claim(fn, ud)`.
   Once a non-NULL handler is installed, any `ndx_load()` into that region
   that finds no `ndx_claim` symbol returns `NDX_ERR_EPERM` and
   `ndx_install` never runs.  Passing `NULL, NULL` clears the gate.

---

## Unchanged

```c
// Define a hook (host or module implementation)
NDX_DEF(ftype, fname, ...)

// Declare a hook (consumer-side: generates call_ wrapper only)
NDX_DECL(ftype, fname, ...)

// Dispatch to all modules implementing fname whose region is a
// descendant-or-equal of the caller's current region
NDX_CALL(retp, fname, ...)

// Pledge exclusive call rights to a hook within the caller's current region.
// First caller wins; subsequent pledges for the same hook return NDX_ERR_EPERM.
// Any other caller invoking the hook in that region receives NDX_ERR_EPERM.
int ndx_pledge(const char *hook_name);

// Lifecycle
void        ndx_init(void);
void        ndx_shutdown(void);
int         ndx_errno(void);
const char *ndx_strerror(int err);

// Adapter registration — called automatically by NDX_DEF constructors
unsigned ndx_areg(char *name, ndx_adapter_t *adapter);
```

---

## Changed signatures

```c
// Load a module into the caller's current region.
// (region_id argument removed)
int ndx_load(char *fname);

// Deny a hook name or module path within the caller's current region
// and all its descendants (including the caller's own region).
// (region_id and children_only arguments removed)
int ndx_deny(const char *what, ndx_deny_type_t type);

// Install a middleware interceptor on the caller's current region.
// Interceptors are called outermost-first (root -> target region).
// Each interceptor may inspect/modify args and ret, call next to
// continue, or return early to block.
// (region_id argument removed)
int ndx_intercept(const char *hook_name, ndx_interceptor_fn_t *fn, void *ud);
```

---

## New API

```c
// Module-side region declaration (data symbol, not a function call).
//
// A module opts into being a region module by exporting this symbol:
//
//   MODULE_API uint8_t ndx_claim = N;
//
// where N is the requested bit-width of the sub-region.
// The host reads this symbol at load time.  If the parent region has a
// claim handler (or has require_claim set), the host performs the claim
// automatically before running ndx_install().  If neither is set, the
// symbol is ignored and the module loads flat into the parent's region.
//
// This is a linker-visible data symbol — it is NOT a function to call.


// Claim handler type.
//
//   module_path    — path of the child module making the request (read-only)
//   requested_bits — number of bits the child asked for
//   granted_bits   — out-param: write the approved width here to accept
//   ud             — user data supplied to ndx_require_claim
//
// Return NDX_OK to approve (with *granted_bits set), NDX_ERR_EPERM to reject.
typedef int ndx_claim_handler_fn_t(
    const char *module_path,
    uint8_t     requested_bits,
    uint8_t    *granted_bits,
    void       *ud);


// Register a claim handler on the caller's current region AND enforce the
// claim contract (require_claim gate) in one call.
//
// fn != NULL:
//   Sets require_claim = 1 and installs fn as the handler.
//   Any ndx_load() into this region that finds no `ndx_claim` symbol is
//   rejected immediately (ndx_install() never runs) and ndx_load() returns
//   NDX_ERR_EPERM.  When the symbol is present the host invokes fn to approve
//   or deny the request before running ndx_install().
//   Only one handler per region; a second non-NULL call replaces the first.
//
// fn == NULL (both fn and ud must be NULL):
//   Clears require_claim = 0 and sets the handler to NULL.
//   Subsequent ndx_load() calls no longer require ndx_claim; modules with the
//   symbol still load flat (no auto-claim) until a new handler is installed.
//
// Modules loaded before the first ndx_require_claim() in the same
// ndx_install() context are unaffected.
//
// Returns NDX_OK, or NDX_ERR_NOTFOUND if the current region cannot be found.
int ndx_require_claim(ndx_claim_handler_fn_t *fn, void *ud);


// Enumerate immediate child regions of the caller's current region.
//
// Calls fn(child_id, ud) for each child, in allocation order.
// child_id is an opaque uint64_t; it is provided for diagnostic and
// logging use only and must not be stored, compared, or passed to any
// other API function.
//
// Return NDX_OK from fn to continue iteration, any other value to stop.
// ndx_region_each returns the last value returned by fn, or NDX_OK if
// there were no children.
typedef int ndx_region_each_fn_t(uint64_t child_id, void *ud);
int ndx_region_each(ndx_region_each_fn_t *fn, void *ud);
```

---

## struct ndx_ctx

Injected into every module at load time. Modules access it as the global `ndx`
variable (provided by `ndx-mod.h`).

```c
struct ndx_ctx {
    ndx_call_t             *call;
    ndx_areg_t             *areg;
    ndx_load_t             *load;        // int (*)(char *)
    ndx_errno_t            *err;
    ndx_strerror_t         *strerror;
    ndx_adapter_t          *adapter;
    ndx_last_t             *last;
    ndx_shutdown_t         *shutdown;
    const char             *module_path; // set by host at load time; read-only
    ndx_pledge_t           *pledge;
    ndx_set_caller_t       *set_caller;
    uint64_t                region_id;   // internal; do not use directly
    ndx_deny_t             *deny;        // int (*)(const char *, ndx_deny_type_t)
    ndx_intercept_t        *intercept;   // int (*)(const char *, fn, ud)
    ndx_require_claim_t    *require_claim;
    ndx_region_each_t      *region_each;
};
```

Field order from `call` through `region_id` is frozen for ABI compatibility
with existing modules. New fields follow `region_id` and must match `papi.h`'s
`ndx_t` exactly.

---

## Region ID encoding (internal, not public)

Region IDs are plain opaque `uint64_t` values — all 64 bits are address
space. The prefix length (`plen`) is stored in the internal
`ndx_region_entry_t` struct, not packed into the ID itself.

- `NDX_REGION_ROOT = 0`: `plen = 0` in its entry; matches everything
  (ancestor of all regions).
- Ancestry check (O(1)): build a top-N-bits mask from the ancestor's
  `plen` (`mask = ~((1 << (64 - plen)) - 1)`), then check
  `(child_id & mask) == ancestor_id`.  No pointer walk.
- Child allocation: given a parent with `plen` and a requested `bits`
  width, the child's `plen` is `parent.plen + bits`.  The child ID is
  `parent.id | (slot << (64 - child_plen))` where `slot` is the lowest
  index with no existing region at that ID.
- Maximum cumulative depth: 64 bits total across all nesting levels
  (e.g. 64 levels of 1-bit claims, or 1 level of 64 bits).
- Maximum branching at one level: `2^bits` children per
  `ndx_claim(bits)` call.

---

## Error codes

| Code | Value | Meaning |
|---|---|---|
| `NDX_OK` | 0 | Success |
| `NDX_ERR_NOTFOUND` | -1 | Module or hook not found |
| `NDX_ERR_INVALID` | -2 | Invalid argument |
| `NDX_ERR_TOOBIG` | -3 | Return type too large / no slot available |
| `NDX_ERR_INIT` | -4 | Initialization failed |
| `NDX_ERR_EPERM` | -5 | Operation not permitted (pledge or claim violation) |

---

## Usage patterns

### Non-region module (unchanged from before)

```c
// mods/combat_log.c
#include <ttypt/ndx-mod.h>
#include "game_hooks.h"

NDX_DEF(int, on_damage, int, player_id, int, damage) {
    printf("Player %d took %d damage\n", player_id, damage);
    return 0;
}

void ndx_install(void) {}
```

### Region moderator (top-level "god")

```c
// mods/mod_universe.c
#include <ttypt/ndx-mod.h>

static int universe_claim_handler(const char *module_path,
                                   uint8_t requested, uint8_t *granted,
                                   void *ud) {
    (void)module_path; (void)ud;
    // Allow sub-regions of at most 1 bit (halves only)
    *granted = requested <= 1 ? requested : 1;
    return NDX_OK;
}

void ndx_install(void) {
    ndx_require_claim(universe_claim_handler, NULL);
    ndx_load("mods/mod_mortality");      // has ndx_claim=1 → auto-claimed into a half
    ndx_load("mods/mod_something_else"); // has ndx_claim=1 → auto-claimed into the other half
}
```

### Region module (mortality sub-moderator)

```c
// mods/mod_mortality.c
#include <ttypt/ndx-mod.h>
#include "game_hooks.h"

// Declare this module as a region module requesting a 1-bit sub-region.
// The host reads this at load time and performs the claim automatically
// before calling ndx_install() — no ndx_claim() call is needed here.
MODULE_API uint8_t ndx_claim = 1;

static int death_claim_handler(const char *module_path,
                                uint8_t requested, uint8_t *granted,
                                void *ud) {
    (void)module_path; (void)ud;
    // Allow sub-regions of at most 2 bits (fourths of this half)
    *granted = requested <= 2 ? requested : 2;
    return NDX_OK;
}

NDX_DEF(void, on_death, int, entity_id) {
    // Dispatch reaches all sub-handlers in descendant regions automatically
    NDX_CALL(NULL, on_death, entity_id);
}

void ndx_install(void) {
    // Already operating in our own sub-region — the host claimed it for us.
    ndx_require_claim(death_claim_handler, NULL);

    ndx_load("mods/mod_loot");    // has ndx_claim=2 → auto-claimed into a quarter
    ndx_load("mods/mod_respawn"); // has ndx_claim=2 → auto-claimed into another quarter
}
```

### Host

```c
// host.c
#include "game_hooks.h"

NDX_DEF(void, on_death, int, entity_id);

int main(void) {
    ndx_load("mods/mod_universe"); // loads transitively

    // Dispatches into root — reaches all descendant regions automatically
    call_on_death(42);

    ndx_shutdown();
    return 0;
}
```
