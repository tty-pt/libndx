# libndx

A small C library for hook-based extensibility and dynamic module loading.

The host program defines **hooks** — typed dispatch points — and loads **modules** (shared libraries) that implement them. Calling a hook dispatches to every loaded module that provides it. Modules can load other modules, deny hooks, intercept calls, and claim isolated address regions.

---

## Installation

See [these instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md#install-ttypt-packages) and use `libndx` as the package name.

---

## Concepts

### Hooks

A hook is a named, typed function that any number of modules can implement. The host declares the hook's signature once; each module that wants to handle it provides an implementation. When the hook is called, all implementing modules run.

### NDX_DECL vs NDX_DEF

- **`NDX_DECL`** — goes in shared headers. Declares the hook signature and generates the `call_*` wrapper. Use it in any translation unit that needs to call the hook but does not implement it.
- **`NDX_DEF`** — goes in `.c` files. Does everything `NDX_DECL` does, plus registers the adapter and provides the function body. The host must use `NDX_DEF` in at least one translation unit to allocate the adapter; modules use it to provide their implementation.

### Host vs Module

- The **host** is the main executable. It defines hooks with `NDX_DEF`, loads modules with `ndx_load`, and dispatches with `call_*`.
- A **module** is a shared library (`.so`/`.dll`). It includes `<ttypt/ndx-mod.h>`, implements hooks with `NDX_DEF`, and exports `ndx_install()` which is called once on first load.

### Regions

Regions are isolated namespaces within the module graph. A `call_*` dispatches only to modules whose region is a descendant-or-equal of the caller's current region. The root region (region 0) reaches every module.

A module opts into having its own region by calling `ndx_claim(bits)` from `ndx_install()`. The parent region must have a claim handler registered via `ndx_on_claim`; without one, `ndx_claim` always fails. Once claimed, all subsequent `ndx_load`, `ndx_deny`, `ndx_intercept`, and `ndx_pledge` calls from that module operate on the new child region.

---

## Quick start

**Shared header** (`hooks.h`):

```c
#include <ttypt/ndx.h>

NDX_DECL(int, on_damage, int, player_id, int, amount);
```

**Module** (`mods/combat_log.c`):

```c
#include <ttypt/ndx-mod.h>
#include "hooks.h"

NDX_DEF(int, on_damage, int, player_id, int, amount)
{
    printf("player %d took %d damage\n", player_id, amount);
    return 0;
}

void ndx_install(void) {}
```

**Host** (`host.c`):

```c
#include "hooks.h"

NDX_DEF(int, on_damage, int, player_id, int, amount);

int main(void)
{
    ndx_load("mods/combat_log");
    call_on_damage(1, 50);
    ndx_shutdown();
    return 0;
}
```

**Build:**

```sh
# host
cc -o host host.c -lndx -pthread

# module
cc -o mods/combat_log.so mods/combat_log.c -fPIC -shared -lndx
```

---

## API Reference

### Macros

#### `NDX_DECL(ftype, fname, type, name, ...)`

Declares a hook in a shared header. Generates:
- `fname_t` — function typedef
- `call_fname(...)` — typed dispatch wrapper (calls `NDX_CALL` internally)

Parameters alternate as `type, name` pairs. At least one pair is required.

```c
NDX_DECL(int, on_tick, int, dt);
// Generates: call_on_tick(int dt)
```

#### `NDX_DEF(ftype, fname, type, name, ...)`

Defines a hook implementation. Generates everything `NDX_DECL` does, plus:
- Registers the adapter automatically at startup (via `.init_array`)
- Opens the function body — write the implementation immediately after

```c
NDX_DEF(int, on_tick, int, dt)
{
    return dt * 2;
}
```

#### `NDX_CALL(retp, fname, ...)`

Dispatches the hook to all eligible modules. `retp` receives the return value of the last module that ran (zero-initialised if none ran).

```c
int result;
NDX_CALL(&result, on_tick, 16);
```

In module context, use `call_fname(...)` instead; it routes through the injected `ndx` context and sets the caller identity correctly.

---

### Lifecycle

#### `int ndx_load(char *fname)`

Load a module into the caller's current region. `fname` is the path to the shared library, without the `.so`/`.dll` extension.

On first load, `ndx_install()` is called. Subsequent loads of the same path into the same region are no-ops.

Returns `NDX_OK` on success, negative on failure.

```c
ndx_load("mods/combat_log");
```

#### `void ndx_shutdown(void)`

Unload all modules and free resources. After this, `ndx_load` can be called again.

#### `void ndx_init(void)`

Explicit initialisation. Called automatically on first use; only needed if you want to control when initialisation happens.

---

### Errors

#### `int ndx_errno(void)`

Returns the last error code set by any API function.

#### `const char *ndx_strerror(int err)`

Returns a static human-readable string for an error code.

```c
int r = ndx_load("mods/missing");
if (r != NDX_OK)
    fprintf(stderr, "load failed: %s\n", ndx_strerror(r));
```

**Error codes:**

| Code | Value | Meaning |
|---|---|---|
| `NDX_OK` | 0 | Success |
| `NDX_ERR_NOTFOUND` | -1 | Module or hook not found |
| `NDX_ERR_INVALID` | -2 | Invalid argument |
| `NDX_ERR_TOOBIG` | -3 | Return type too large / no free region slot |
| `NDX_ERR_INIT` | -4 | Initialisation failed |
| `NDX_ERR_EPERM` | -5 | Not permitted (pledge or claim violation) |

---

### Pledge

#### `int ndx_pledge(const char *hook_name)`

Restrict who may call a hook. The first module to pledge a hook name within its region becomes the sole permitted caller of that hook in that region. Any other caller attempting to dispatch the hook receives `NDX_ERR_EPERM`.

Must be called from `ndx_install()`.

A second pledge for the same hook in the same region returns `NDX_ERR_EPERM`.

```c
// In ndx_install — only this module may call "get_token" within this region
ndx_pledge("get_token");
```

---

### Regions

All region functions operate on the caller's **current region**, set implicitly by the dispatch and load machinery. There are no explicit region-ID parameters.

#### `int ndx_claim(uint8_t bits)`

Claim a child region of `bits` width under the caller's current region. Only valid inside `ndx_install()`.

The parent's claim handler is invoked first. It may approve, reduce `bits`, or reject. If no handler is registered, the call always fails with `NDX_ERR_EPERM`.

On success:
- The module's region becomes the newly created child.
- All subsequent `ndx_load`, `ndx_deny`, `ndx_intercept`, `ndx_pledge`, and `ndx_on_claim` calls operate on the child region.

Returns `NDX_OK`, `NDX_ERR_EPERM` (rejected or no handler), or `NDX_ERR_TOOBIG` (no free slot).

```c
void ndx_install(void)
{
    if (ndx_claim(4) != NDX_OK) return; // request 4-bit slice
    // now in our own region
    ndx_load("mods/sub_module");
}
```

#### `int ndx_on_claim(ndx_claim_handler_fn_t *fn, void *ud)`

Register a claim handler on the caller's current region. Only one handler per region; a second call replaces the first.

The handler is invoked whenever a module loaded into this region calls `ndx_claim()`.

```c
typedef int ndx_claim_handler_fn_t(
    const char *module_path,   // path of the requesting module (read-only)
    uint8_t     requested_bits,
    uint8_t    *granted_bits,  // write the approved width here
    void       *ud
);
```

Return `NDX_OK` to approve (with `*granted_bits` set), or `NDX_ERR_EPERM` to reject.

```c
static int my_handler(const char *path, uint8_t req,
                       uint8_t *granted, void *ud)
{
    (void)path; (void)ud;
    *granted = req > 8 ? 8 : req; // cap at 8 bits
    return NDX_OK;
}

void ndx_install(void)
{
    ndx_on_claim(my_handler, NULL);
    ndx_load("mods/child"); // child may now call ndx_claim(<=8)
}
```

#### `int ndx_deny(const char *what, ndx_deny_type_t type)`

Block a hook or module within the caller's current region and all its descendants. The denial applies to the caller's own region as well — any call dispatched from within the denying region (or any descendant) is affected.

`type` is one of:

| Value | Meaning |
|---|---|
| `NDX_DENY_HOOK` | `what` is a hook name |
| `NDX_DENY_MODULE` | `what` is a module path |

```c
// Prevent any module in this region or sub-regions from calling "dangerous_hook"
ndx_deny("dangerous_hook", NDX_DENY_HOOK);

// Prevent a specific module from running in this region or sub-regions
ndx_deny("mods/untrusted", NDX_DENY_MODULE);
```

#### `int ndx_intercept(const char *hook_name, ndx_interceptor_fn_t *fn, void *ud)`

Register a middleware interceptor for `hook_name` in the caller's current region. Interceptors run outermost-first (root region before child regions). Each interceptor may inspect or modify arguments and the return value, call `next` to continue, or return early to block.

```c
typedef int ndx_interceptor_fn_t(
    const char   *hook,     // hook name
    void         *args,     // cast to struct fname_args*
    void         *ret,      // cast to the hook's return type
    ndx_call_fn_t next,     // call to continue the chain
    void         *next_ud,  // pass verbatim to next (do not modify)
    void         *ud        // user data from ndx_intercept
);
```

```c
static int logging_interceptor(const char *hook, void *args, void *ret,
                                ndx_call_fn_t next, void *next_ud, void *ud)
{
    (void)args; (void)ret; (void)ud;
    fprintf(stderr, "before %s\n", hook);
    next(args, ret, next_ud);
    fprintf(stderr, "after %s\n", hook);
    return NDX_OK;
}

void ndx_install(void)
{
    ndx_intercept("on_tick", logging_interceptor, NULL);
}
```

#### `int ndx_region_each(ndx_region_each_fn_t *fn, void *ud)`

Enumerate immediate child regions of the caller's current region. Calls `fn(child_id, ud)` for each child. `child_id` is an opaque `uint64_t` — it is provided for diagnostic and logging purposes only.

Return `NDX_OK` from `fn` to continue, any other value to stop. `ndx_region_each` returns the last value returned by `fn`, or `NDX_OK` if there were no children.

```c
typedef int ndx_region_each_fn_t(uint64_t child_id, void *ud);
```

```c
static int print_child(uint64_t id, void *ud)
{
    (void)ud;
    fprintf(stderr, "child region: %016llx\n", (unsigned long long)id);
    return NDX_OK;
}

ndx_region_each(print_child, NULL);
```

#### `ndx_my_region()`

Macro. Returns the `uint64_t` region ID assigned to the calling module. Available in module context only (requires `<ttypt/ndx-mod.h>`). Intended for diagnostic use.

```c
fprintf(stderr, "my region: %016llx\n",
        (unsigned long long)ndx_my_region());
```

---

## Region walkthrough

This example shows a moderator module that controls a child region, an interceptor, and a deny.

**Shared hook header** (`game_hooks.h`):

```c
#include <ttypt/ndx.h>
NDX_DECL(int, on_tick, int, dt);
```

**Worker module** (`mods/worker.c`) — lives in the child region:

```c
#include <ttypt/ndx-mod.h>
#include "game_hooks.h"

NDX_DEF(int, on_tick, int, dt)
{
    printf("worker tick: dt=%d\n", dt);
    return dt;
}

void ndx_install(void) {}
```

**Moderator module** (`mods/moderator.c`):

```c
#include <ttypt/ndx-mod.h>
#include "game_hooks.h"

/* Interceptor: halves dt before passing it on */
static int halving_interceptor(const char *hook, void *args, void *ret,
                                ndx_call_fn_t next, void *next_ud, void *ud)
{
    (void)hook; (void)ret; (void)ud;
    struct on_tick_args *a = args;
    a->dt /= 2;
    next(args, ret, next_ud);
    return NDX_OK;
}

/* Claim handler: approve up to 4 bits */
static int claim_handler(const char *path, uint8_t req,
                          uint8_t *granted, void *ud)
{
    (void)path; (void)ud;
    *granted = req > 4 ? 4 : req;
    return NDX_OK;
}

void ndx_install(void)
{
    /* Register handler so child modules may claim */
    ndx_on_claim(claim_handler, NULL);

    /* Claim a 1-bit slice for this module */
    if (ndx_claim(1) != NDX_OK) return;

    /* Now in our own region: install interceptor and load worker */
    ndx_intercept("on_tick", halving_interceptor, NULL);
    ndx_load("mods/worker");
}
```

**Host** (`host.c`):

```c
#include "game_hooks.h"

NDX_DEF(int, on_tick, int, dt);

int main(void)
{
    ndx_load("mods/moderator");

    /* Dispatches root → moderator's region → worker */
    call_on_tick(100); // worker sees dt=50 after interceptor

    ndx_shutdown();
    return 0;
}
```

When `call_on_tick(100)` is dispatched from root:
1. The halving interceptor (registered in moderator's region) fires first, setting `dt = 50`.
2. The worker's `on_tick` runs with `dt = 50`.
