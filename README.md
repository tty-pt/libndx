# libxylem

A small C library for hook-based extensibility and dynamic module loading.

The host program defines **hooks** — typed dispatch points — and loads **modules** (shared libraries) that implement them. Calling a hook dispatches to every loaded module that provides it. Modules can load other modules, deny hooks, intercept calls, and claim isolated address regions.

---

## Installation

See [these instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md#install-ttypt-packages) and use `libxylem` as the package name.

---

## Concepts

### Hooks

A hook is a named, typed function that any number of modules can implement. The host declares the hook's signature once; each module that wants to handle it provides an implementation. When the hook is called, all implementing modules run.

### `XY_DECL` vs `XY_DEF` vs `XY_IMPL`

- **`XY_DECL`** — goes in shared headers. Declares a hook that callers invoke as a normal function.
- **`XY_DEF`** — goes in one host `.c` file. Emits the canonical adapter for that hook and provides the same normal-function call syntax in that translation unit.
- **`XY_IMPL`** — goes in module `.c` files. Registers a module-side listener implementation for a hook.

### Host vs Module

- The **host** is the main executable. It defines hooks with `XY_DEF`, loads modules with `xy_load`, and dispatches by calling the hook function directly.
- A **module** is a shared library (`.so`/`.dll`). It includes `<ttypt/xy-mod.h>`, implements hooks with `XY_IMPL`, and exports `xy_install()` which is called once on first load.

### Regions

Regions are isolated namespaces within the module graph. A `call_*` dispatches only to modules whose region is a descendant-or-equal of the caller's current region. The root region (region 0) reaches every module.

A module opts into having its own region by calling `xy_claim(bits)` from `xy_install()`. The parent region must have a claim handler registered via `xy_require_claim`; without one, `xy_claim` always fails. Once claimed, all subsequent `xy_load`, `xy_deny`, `xy_intercept`, and `xy_pledge` calls from that module operate on the new child region.

---

## Quick start

**Shared header** (`hooks.h`):

```c
#include <ttypt/xy.h>

XY_DECL(int, on_damage, int, player_id, int, amount);
```

**Module** (`mods/combat_log.c`):

```c
#include <ttypt/xy-mod.h>
#include "hooks.h"

XY_IMPL(int, on_damage, int, player_id, int, amount)
{
    printf("player %d took %d damage\n", player_id, amount);
    return 0;
}

void xy_install(void) {}
```

**Host** (`host.c`):

```c
#include "hooks.h"

XY_DEF(int, on_damage, int, player_id, int, amount);

int main(void)
{
    xy_load("mods/combat_log");
    on_damage(1, 50);
    xy_shutdown();
    return 0;
}
```

**Build:**

```sh
# host
cc -o host host.c -lxylem -pthread

# module
cc -o mods/combat_log.so mods/combat_log.c -fPIC -shared -lxylem
```

---

## API Reference

### Macros

#### `XY_DECL(ftype, fname, type, name, ...)`

Declares a hook in a shared header. Generates:
- `fname_t` — function typedef
- `call_fname(...)` — typed dispatch wrapper (calls `XY_CALL` internally)

Parameters alternate as `type, name` pairs. At least one pair is required.

```c
XY_DECL(int, on_tick, int, dt);
// Generates: call_on_tick(int dt)
```

#### `XY_DEF(ftype, fname, type, name, ...)`

Defines the canonical host-side hook adapter. The host calls the hook
directly as a normal function.

```c
XY_DEF(int, on_tick, int, dt);
```

#### `XY_IMPL(ftype, fname, type, name, ...)`

Defines a module listener implementation. Registers the adapter automatically
at startup (via `.init_array`) and opens the function body immediately after.

```c
XY_IMPL(int, on_tick, int, dt)
{
    return dt * 2;
}
```

#### `XY_CALL(retp, fname, ...)`

Dispatches the hook to all eligible modules. `retp` receives the return value of the last module that ran (zero-initialised if none ran).

```c
int result;
XY_CALL(&result, on_tick, 16);
```

In module context, hook calls use the normal function form as well; the
generated inline dispatch routes through the injected `xy` context and sets
the caller identity correctly.

---

### Lifecycle

#### `int xy_load(char *fname)`

Load a module into the caller's current region. `fname` is the path to the shared library, without the `.so`/`.dll` extension.

On first load, `xy_install()` is called. Subsequent loads of the same path into the same region are no-ops.

Returns `XY_OK` on success, negative on failure.

```c
xy_load("mods/combat_log");
```

#### `void xy_shutdown(void)`

Unload all modules and free resources. After this, `xy_load` can be called again.

#### `void xy_init(void)`

Explicit initialisation. Called automatically on first use; only needed if you want to control when initialisation happens.

---

### Errors

#### `int xy_errno(void)`

Returns the last error code set by any API function.

#### `const char *xy_strerror(int err)`

Returns a static human-readable string for an error code.

```c
int r = xy_load("mods/missing");
if (r != XY_OK)
    fprintf(stderr, "load failed: %s\n", xy_strerror(r));
```

**Error codes:**

| Code | Value | Meaning |
|---|---|---|
| `XY_OK` | 0 | Success |
| `XY_ERR_NOTFOUND` | -1 | Module or hook not found |
| `XY_ERR_INVALID` | -2 | Invalid argument |
| `XY_ERR_TOOBIG` | -3 | Return type too large / no free region slot |
| `XY_ERR_INIT` | -4 | Initialisation failed |
| `XY_ERR_EPERM` | -5 | Not permitted (pledge or claim violation) |

---

### Pledge

> **⚠️ Not yet implemented.** The `xy_ctx` struct reserves a `pledge` slot for ABI
> compatibility, but the runtime dispatch logic does not enforce pledges yet.
> This section describes the planned semantics.

#### `int xy_pledge(const char *hook_name)`

Restrict who may call a hook. The first module to pledge a hook name within its region becomes the sole permitted caller of that hook in that region. Any other caller attempting to dispatch the hook receives `XY_ERR_EPERM`.

Must be called from `xy_install()`.

A second pledge for the same hook in the same region returns `XY_ERR_EPERM`.

```c
// In xy_install — only this module may call "get_token" within this region
xy_pledge("get_token");
```

---

### Regions

All region functions operate on the caller's **current region**, set implicitly by the dispatch and load machinery. There are no explicit region-ID parameters.

#### `XY_MODULE_API uint8_t xy_claim = N`

Module-side region declaration. A module opts into having its own sub-region by
exporting this data symbol with the requested bit-width `N`.

This is a **data symbol**, not a function. The host reads it at load time via
`dlsym`. If the parent region has a claim handler installed (via
`xy_require_claim`), the host performs the claim automatically before calling
`xy_install()`. If no handler is registered, the symbol is ignored and the
module loads flat into the parent's region.

On success:
- The module's region becomes the newly created child of the parent.
- All subsequent `xy_load`, `xy_deny`, `xy_require_claim` calls operate on the child region.

```c
// mods/sub_mod.c
XY_MODULE_API uint8_t xy_claim = 4; // request a 4-bit sub-region

void xy_install(void)
{
    // already in our own region — host claimed it for us
    xy_load("mods/sub_module");
}
```

#### `int xy_require_claim(xy_claim_handler_fn_t *fn, void *ud)`

Register a claim handler on the caller's current region and enforce the claim contract. Only one handler per region; a second non-NULL call replaces the first.

When `fn` is non-NULL: any `xy_load()` into this region whose module exports no `xy_claim` symbol is rejected immediately (`xy_install` never runs) and `xy_load` returns `XY_ERR_EPERM`. When the symbol is present, `fn` is invoked to approve or deny before `xy_install` runs.

When both `fn` and `ud` are NULL: clears the gate — subsequent loads no longer require `xy_claim`.

```c
typedef int xy_claim_handler_fn_t(
    const char *module_path,   // path of the requesting module (read-only)
    uint8_t     requested_bits,
    uint8_t    *granted_bits,  // write the approved width here
    void       *ud
);
```

Return `XY_OK` to approve (with `*granted_bits` set), or `XY_ERR_EPERM` to reject.

```c
static int my_handler(const char *path, uint8_t req,
                       uint8_t *granted, void *ud)
{
    (void)path; (void)ud;
    *granted = req > 8 ? 8 : req; // cap at 8 bits
    return XY_OK;
}

void xy_install(void)
{
    xy_require_claim(my_handler, NULL);
    xy_load("mods/child"); // child must export xy_claim; approved up to 8 bits
}
```

#### `int xy_deny(const char *what, xy_deny_type_t type)`

Block a hook or module within the caller's current region and all its descendants. The denial applies to the caller's own region as well — any call dispatched from within the denying region (or any descendant) is affected.

`type` is one of:

| Value | Meaning |
|---|---|
| `XY_DENY_HOOK` | `what` is a hook name |
| `XY_DENY_MODULE` | `what` is a module path |

```c
// Prevent any module in this region or sub-regions from calling "dangerous_hook"
xy_deny("dangerous_hook", XY_DENY_HOOK);

// Prevent a specific module from running in this region or sub-regions
xy_deny("mods/untrusted", XY_DENY_MODULE);
```

#### `int xy_intercept(const char *hook_name, xy_interceptor_fn_t *fn, void *ud)`

> **⚠️ Not yet implemented.** The `xy_ctx` struct reserves an `intercept` slot
> for ABI compatibility, but the dispatch loop does not call interceptors yet.
> This section describes the planned semantics.

Register a middleware interceptor for `hook_name` in the caller's current region. Interceptors run outermost-first (root region before child regions). Each interceptor may inspect or modify arguments and the return value, call `next` to continue, or return early to block.

```c
typedef int xy_interceptor_fn_t(
    const char   *hook,     // hook name
    void         *args,     // cast to struct fname_args*
    void         *ret,      // cast to the hook's return type
    xy_call_fn_t next,     // call to continue the chain
    void         *next_ud,  // pass verbatim to next (do not modify)
    void         *ud        // user data from xy_intercept
);
```

```c
static int logging_interceptor(const char *hook, void *args, void *ret,
                                xy_call_fn_t next, void *next_ud, void *ud)
{
    (void)args; (void)ret; (void)ud;
    fprintf(stderr, "before %s\n", hook);
    next(args, ret, next_ud);
    fprintf(stderr, "after %s\n", hook);
    return XY_OK;
}

void xy_install(void)
{
    xy_intercept("on_tick", logging_interceptor, NULL);
}
```

#### `int xy_region_each(xy_region_each_fn_t *fn, void *ud)`

Enumerate immediate child regions of the caller's current region. Calls `fn(child_id, ud)` for each child. `child_id` is an opaque `uint64_t` — it is provided for diagnostic and logging purposes only.

Return `XY_OK` from `fn` to continue, any other value to stop. `xy_region_each` returns the last value returned by `fn`, or `XY_OK` if there were no children.

```c
typedef int xy_region_each_fn_t(uint64_t child_id, void *ud);
```

```c
static int print_child(uint64_t id, void *ud)
{
    (void)ud;
    fprintf(stderr, "child region: %016llx\n", (unsigned long long)id);
    return XY_OK;
}

xy_region_each(print_child, NULL);
```

#### `xy_my_region()`

Macro. Returns the `uint64_t` region ID assigned to the calling module. Available in module context only (requires `<ttypt/xy-mod.h>`). Intended for diagnostic use.

```c
fprintf(stderr, "my region: %016llx\n",
        (unsigned long long)xy_my_region());
```

---

## Region walkthrough

This example shows a moderator module that controls a child region, an interceptor, and a deny.

**Shared hook header** (`game_hooks.h`):

```c
#include <ttypt/xy.h>
XY_DECL(int, on_tick, int, dt);
```

**Worker module** (`mods/worker.c`) — lives in the child region:

```c
#include <ttypt/xy-mod.h>
#include "game_hooks.h"

XY_IMPL(int, on_tick, int, dt)
{
    printf("worker tick: dt=%d\n", dt);
    return dt;
}

void xy_install(void) {}
```

**Moderator module** (`mods/moderator.c`):

```c
#include <ttypt/xy-mod.h>
#include "game_hooks.h"

/* Interceptor: halves dt before passing it on */
static int halving_interceptor(const char *hook, void *args, void *ret,
                                xy_call_fn_t next, void *next_ud, void *ud)
{
    (void)hook; (void)ret; (void)ud;
    struct on_tick_args *a = args;
    a->dt /= 2;
    next(args, ret, next_ud);
    return XY_OK;
}

/* Claim handler: approve up to 4 bits */
static int claim_handler(const char *path, uint8_t req,
                          uint8_t *granted, void *ud)
{
    (void)path; (void)ud;
    *granted = req > 4 ? 4 : req;
    return XY_OK;
}

/* Request a 1-bit sub-region — data symbol, not a function call */
XY_MODULE_API uint8_t xy_claim = 1;

void xy_install(void)
{
    /* Register handler so child modules may claim */
    xy_require_claim(claim_handler, NULL);

    /* Already in our own region — host claimed it for us.
     * Now we can install interceptor and load worker. */
    xy_intercept("on_tick", halving_interceptor, NULL);
    xy_load("mods/worker");
}
```

**Host** (`host.c`):

```c
#include "game_hooks.h"

XY_DEF(int, on_tick, int, dt);

int main(void)
{
    xy_load("mods/moderator");

    /* Dispatches root → moderator's region → worker */
    call_on_tick(100); // worker sees dt=50 after interceptor

    xy_shutdown();
    return 0;
}
```

When `call_on_tick(100)` is dispatched from root:
1. The halving interceptor (registered in moderator's region) fires first, setting `dt = 50`.
2. The worker's `on_tick` runs with `dt = 50`.
