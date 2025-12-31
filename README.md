# libndx
> A small library for modding and extensibility.

## Installation
Check out [these instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md#install-ttypt-packages).
And use "libndx" as the package name.

## Architecture

libndx uses a two-sided architecture:

```
┌─────────────────┐         ┌─────────────────┐
│     HOST        │         │     MODULE      │
│   (executable)  │         │   (.so/.dll)    │
├─────────────────┤         ├─────────────────┤
│ NDX_DEF         │◄────────│ NDX_DECL        │
│ (define hook)   │  calls  │ (declare hook)  │
│ ndx_load()      │────────►│ ndx_install()   │
│ ndx_call()      │         │ get_ndx_ptr()   │
└─────────────────┘         └─────────────────┘
```

- **Host** - The main application that defines hooks and loads modules
- **Module** - A shared library that implements hooks
- **Adapter** - Bridge between generic `void*` dispatch and typed functions

## Quick Start

**Host:**
```c
#include <ttypt/ndx.h>

NDX_DEF(int, on_tick, int, dt);

static void
register_ndx(void)
{
	on_tick_adapter_reg();
}

int main(void)
{
	register_ndx();
	ndx_load("./core.so");
	call_on_tick(16);
	return 0;
}
```

**Module:**
```c
#include <ttypt/ndx.h>
#include "src/papi.h"

ndx_t ndx;  // Host populates this via get_ndx_ptr()

NDX_DECL(int, on_tick, int, dt);

int on_tick(int dt)
{
	return dt;
}

void ndx_install(void)
{
	// first load only
}

void ndx_open(void)
{
	// subsequent loads (optional)
}

ndx_t* get_ndx_ptr(void)
{
	return &ndx;  // Host uses this to inject API functions
}
```

`ndx_load()` runs `ndx_install()` on first load and `ndx_open()` on reload (if present).

## Host-Side API

| Function | Description |
|----------|-------------|
| `NDX_DEF(type, name, args...)` | Define a hook adapter |
| `NDX_DECL(type, name, args...)` | Declare a hook (without adapter) |
| `NDX_CALL(retp, name, args...)` | Call a hook by name (example: `NDX_CALL(&ret, on_tick, 16)`) |
| `ndx_load(path)` | Load/reload a module (returns error code) |
| `ndx_areg(name, adapter)` | Register an adapter manually |
| `ndx_get(name)` | Look up adapter ID by name |
| `ndx_last(ret)` | Get last hook's return value |
| `ndx_errno()` | Get last error code |
| `ndx_strerror(err)` | Human-readable error string |
| `ndx_shutdown()` | Clean up resources (closes modules on POSIX) |

### NDX_DEF Expansion

`NDX_DEF(int, on_tick, int, dt)` creates:
- `on_tick_t` - Function typedef
- `struct on_tick_args` - Argument struct
- `on_tick_adapter` - Adapter struct
- `on_tick_adapter_reg()` - Registration function
- `on_tick_id` - Adapter ID (set after registration)
- `call_on_tick(dt)` - Typed call wrapper

### Error Codes

| Code | Constant | Description |
|------|----------|-------------|
| 0 | `NDX_OK` | Success |
| -1 | `NDX_ERR_NOTFOUND` | Module/hook not found |
| -2 | `NDX_ERR_INVALID` | Invalid argument |
| -3 | `NDX_ERR_TOOBIG` | Return type > 4096 bytes |
| -4 | `NDX_ERR_INIT` | Initialization failed |
| -5 | `NDX_ERR_LOCK` | Lock error |
| -6 | `NDX_ERR_CYCLE` | Circular dependency |
| -7 | `NDX_ERR_DEPS` | Dependency error |

## Module-Side API

| Function | Description |
|----------|-------------|
| `NDX_DECL(type, name, args...)` | Declare a hook to implement |
| `get_ndx_ptr()` | Export `ndx_t*` to receive host function pointers |
| `mod_auto_init()` | Optional: Called after load, before install/open |
| `ndx_install()` | Called on first load |
| `ndx_open()` | Optional: Called on reload |
| `ndx_deps[]` | Optional: NULL-terminated array of dependency paths |

### Module Initialization Lifecycle

When `ndx_load("module.so")` is called, the following happens in order:

1. `dlopen()` - Load the shared library
2. `get_ndx_ptr()` - Host populates `ndx_t*` with function pointers
3. Load dependencies from `ndx_deps[]` (if present)
4. `mod_auto_init()` - Optional auto-initialization hook
5. `ndx_install()` - First load only, **OR** `ndx_open()` - Subsequent loads (optional)

## Linking

- **Host** links against `libndx`
- **Modules** link against `libndx-mod`

## Windows Notes
- Auto-init sections are not available, so call your `*_adapter_reg()` functions explicitly.
- Use `.dll` when calling `ndx_load()` (for example, `ndx_load("core.dll")`).

## Thread Safety

All public functions are mutex-protected. Multiple threads can safely:
- Load modules concurrently
- Call hooks concurrently
- Register adapters concurrently

---

## Library Usage (Host-focused)

This section shows the common usage pattern for a host program that wants to expose hooks and load modules dynamically.

1) Define and register the adapter signature

 - In your host source, define the hook signature with the provided macro. Example:

 ```c
 #include <ttypt/ndx.h>

 NDX_DEF(int, on_tick, int, dt);

 int main(void) {
     /* register adapter (creates the adapter id) */
     on_tick_adapter_reg();
     ...
 }
 ```

 The `NDX_DEF` macro creates `on_tick_adapter_reg()`, an `on_tick_id` symbol and the typed helper `call_on_tick(dt)`. Note: Use `NDX_DEF` in the host; modules use `NDX_DECL`.

2) Load modules

 - Use `ndx_load(path)` to load a module file. The loader will: dlopen the file, wire the module's `ndx_t` by calling `get_ndx_ptr()` if present, load any `ndx_deps[]` recursively, run `ndx_install()` (first-time) or `ndx_open()` (reload), and detect cycles.

 ```c
 if (ndx_load("./mods/mod_basic.so") != NDX_OK) {
     fprintf(stderr, "failed to load module: %s\n", ndx_strerror(ndx_errno()));
 }
 ```

3) Call hooks

 - Use the generated helper `call_<name>(...)` to call the hook. The macro packs arguments and dispatches to all modules that implement the named function.

 ```c
 int result = call_on_tick(16);
 (void) result; /* last-run adapter return copied into result */
 ```

Low-level functions (you rarely need these)

 - `ndx_areg(char *name, ndx_adapter_t *adapter)` — register an adapter manually; returns an id.
 - `ndx_get(char *name)` — lookup adapter id by name (returns `NDX_INVALID` when missing).
 - `ndx_call(void *retp, unsigned id, void *args)` — low-level call by adapter id.
 - `ndx_last(void *ret)` — fetch the last adapter return buffer.
 - `ndx_load_deps(char *fname)` / `ndx_depends(const char *name)` — manually request loading of a dependency.

Error handling

 - Functions return `NDX_OK` (0) on success or a negative `NDX_ERR_*` value. Use `ndx_errno()` to get the last error and `ndx_strerror(code)` to format it.

Threading notes

 - ndx uses a global mutex internally. Public API functions are thread-safe. Avoid doing long blocking work inside module `ndx_install()` or auto-init callbacks — they may run while the lock is held.

Build and run (Linux example)

 - Build host (example):
 ```sh
 cc -o host host.c -Iinclude -Llib -lndx -lqsys -lqmap -pthread
 ```

 - Build a module (example):
 ```sh
 cc -o mods/mod_basic.so mods/mod_basic.c -Iinclude -fPIC -shared -Llib -lndx-mod
 ```

 - Run the host (ensure runtime linker finds `libndx.so`):
 ```sh
 LD_LIBRARY_PATH=./lib ./host
 ```

Troubleshooting (quick)

 - `call_*` reports `NDX_CALL BAD` / adapter id is `NDX_INVALID`: ensure you called `*_adapter_reg()` before `ndx_load()` or use `ndx_get()` to validate registration.
 - `NDX_ERR_CYCLE` from `ndx_load()`: modules declare circular dependencies in `ndx_deps[]`; inspect and break the cycle.
 - If `ndx_load()` fails to find symbols inside a module, check module exports and that module links against `libndx-mod` and provides `get_ndx_ptr()`.

Examples

 - Minimal host (host.c):
 ```c
 #include <stdio.h>
 #include <ttypt/ndx.h>

 NDX_DEF(int, on_tick, int, dt);

 int main(void) {
     on_tick_adapter_reg();
     if (ndx_load("./mods/mod_basic.so") != NDX_OK) {
         fprintf(stderr, "load failed: %s\n", ndx_strerror(ndx_errno()));
         return 1;
     }
     int r = call_on_tick(10);
     printf("on_tick result=%d\n", r);
     return 0;
 }
 ```

 - Minimal module (mods/mod_basic.c):
 ```c
 #include <ttypt/ndx.h>
 #include "src/papi.h"

 ndx_t ndx;

 MODULE_API int on_tick(int dt) {
     return dt + 1;
 }

 MODULE_API void ndx_install(void) {}

 MODULE_API ndx_t* get_ndx_ptr(void) { return &ndx; }
 ```

Verification

 - Run the internal tests: `make test` (recommended). The dependency tests included exercise `ndx_deps[]` and cycle detection.


## Documentation
- Header: `include/ttypt/ndx.h`
- Host impl: `src/libndx.c`
- Module impl: `src/libndx-mod.c`
- Generate man pages: `make docs` then `man ndx`
