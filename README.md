# libndx
> A small library for modding and extensibility.

## Installation
Check out [these instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md#install-ttypt-packages).
And use "libndx" as the package name.

## Architecture

libndx uses a two-sided architecture:

```

                            ┌─────────────────┐
          ┌─────────────────│  CALEE HEADER   │
          │                 ├─────────────────┤
          │                 │  NDX_DEF        │
          │                 │  (define hook)  │
          │                 └─────────────────┘
          │
┌─────────────────┐         ┌─────────────────┐
│     CALLER      │         │     CALLEE      │
├─────────────────┤         ├─────────────────┤
│ ndx_install()   │◄────────│ NDX_DECL        │
│   ndx_load()    │  calls  │ (declare hook)  │
│ ndx_load()      │────────►│                 │
│ call_*()        │         │ ndx_install()   │
└─────────────────┘         │ ndx_load()      │
                            └─────────────────┘
```

- **Host** - The main application that defines hooks and loads modules
- **Module** - A shared library that implements hooks
- **Adapter** - Bridge between generic `void*` dispatch and typed functions

## Quick Start

**Host:**
```c
#include <ttypt/ndx.h>

NDX_DEF(int, on_tick, int, dt);  // Define hook signature

int main(void)
{
	ndx_load("./core.so");    // Load module - hooks auto-register
	call_on_tick(16);         // Call hook from module
	return 0;
}
```

**Module (implements hooks):**
```c
#include <ttypt/ndx.h>
#include "src/papi.h"

ndx_t ndx;

NDX_DEF(int, on_tick, int, dt);  // Implement this hook

int on_tick(int dt)                // Hook implementation
{
	return dt;
}

void ndx_install(void)             // Called on first load
{
	// initialization
}

void ndx_open(void)               // Called on reload (optional)
{
	// re-initialization
}

ndx_t* get_ndx_ptr(void)          // Export for host to populate
{
	return &ndx;
}
```

`ndx_load()` runs `ndx_install()` on first load and `ndx_open()` on reload (if present).

## Host-Side API

| Function | Description |
|----------|-------------|
| `NDX_DEF(type, name, args...)` | Define a hook |
| `NDX_DECL(type, name, args...)` | Declare a hook to call |
| `ndx_load(path)` | Load/reload a module (returns error code) |
| `ndx_shutdown()` | Clean up resources (closes modules on POSIX) |
| `ndx_errno()` | Get last error code |
| `ndx_strerror(err)` | Human-readable error string |

### NDX_DEF Expansion

`NDX_DEF(int, on_tick, int, dt)` creates:
- `on_tick_t` - Function typedef
- `struct on_tick_args` - Argument struct
- `on_tick_id` - Hook ID (set after registration)
- `call_on_tick(dt)` - Typed call wrapper

### Auto-Init Mechanism

When a module uses `NDX_DEF`, hooks are automatically registered when the module loads.

Example flow:
```c
// In module.c
NDX_DEF(int, my_hook, int, arg);

// When the module loads, hook registration runs automatically
```

### Error Codes

| Code | Constant | Description |
|------|----------|-------------|
| 0 | `NDX_OK` | Success |
| -1 | `NDX_ERR_NOTFOUND` | Module/hook not found |
| -2 | `NDX_ERR_INVALID` | Invalid argument |
| -3 | `NDX_ERR_TOOBIG` | Return type > 4096 bytes |
| -4 | `NDX_ERR_INIT` | Initialization failed |
| -5 | `NDX_ERR_LOCK` | Lock error |

## Module-Side API

| Function | Description |
|----------|-------------|
| `NDX_DEF(type, name, args...)` | Implement a hook (auto-registers) |
| `NDX_DECL(type, name, args...)` | Declare a hook to call |
| `get_ndx_ptr()` | Export `ndx_t*` to receive host function pointers |
| `ndx_install()` | Called on first load |
| `ndx_open()` | Optional: Called on reload |

### Common Patterns

#### Pattern 1: Module Implements Hooks

A module that provides functionality through hooks:

```c
// my_module.c
#include <ttypt/ndx.h>
#include "src/papi.h"

ndx_t ndx;

NDX_DEF(int, calculate, int, value);

int calculate(int value) {
    return value * 2;
}

void ndx_install(void) {
    // Initialize module resources
}

ndx_t* get_ndx_ptr(void) {
    return &ndx;
}
```

#### Pattern 2: Module Calls Hooks from Dependency

A module that uses hooks provided by another module:

```c
// consumer.c
#include <ttypt/ndx.h>
#include "common/hooks.h"  // Has NDX_DECL for hooks
#include "src/papi.h"

ndx_t ndx;

void ndx_install(void) {
    // Load dependency first
    if (ndx_load("./provider.so") != NDX_OK) {
        fprintf(stderr, "Failed to load provider: %s\n", ndx_strerror(ndx_errno()));
        return;
    }
    
    // Now use the hook
    int result;
    NDX_CALL(&result, calculate, 21);
}

ndx_t* get_ndx_ptr(void) {
    return &ndx;
}
```

#### Pattern 3: Shared Hook Header

Define hook signatures in a common header for use across modules:

```c
// common/game_hooks.h
#ifndef GAME_HOOKS_H
#define GAME_HOOKS_H

#include <ttypt/ndx.h>

NDX_DECL(void, on_player_join, int, player_id);
NDX_DECL(void, on_player_leave, int, player_id);
NDX_DECL(int, on_damage, int, player_id, int, damage);

#endif
```

Then each module includes this header and uses NDX_DEF or NDX_CALL as needed.

### Dependencies Between Modules

Dependencies are handled through the API, not automatic loading. Use `NDX_DECL`/`NDX_DEF` to share hook interfaces between modules.

#### Step-by-Step Flow

1. **Shared header** - Define hook signatures with `NDX_DECL`:
   ```c
   // common/player_hooks.h
   #ifndef PLAYER_HOOKS_H
   #define PLAYER_HOOKS_H
   #include <ttypt/ndx.h>
   NDX_DECL(int, get_player_count, void);
   NDX_DECL(void, on_player_join, int, player_id);
   #endif
   ```

2. **Implementing module** - Uses `NDX_DEF` to define hooks, implements functions:
   ```c
   // mods/player_system.c
   #include <ttypt/ndx.h>
   #include "common/player_hooks.h"
   #include "src/papi.h"
   
   ndx_t ndx;
   
   NDX_DEF(int, get_player_count, void);
   int get_player_count(void) { return player_count; }
   
   NDX_DEF(void, on_player_join, int, player_id);
   void on_player_join(int player_id) { ... }
   
   void ndx_install(void) { /* initialize */ }
   ndx_t* get_ndx_ptr(void) { return &ndx; }
   ```

3. **Dependent module** - Includes header, loads dependency with `ndx_load()`, calls hooks:
   ```c
   // mods/game_logic.c
   #include <ttypt/ndx.h>
   #include "common/player_hooks.h"
   #include "src/papi.h"
   
   ndx_t ndx;
   
   void ndx_install(void) {
       if (ndx_load("./player_system.so") != NDX_OK) {
           fprintf(stderr, "failed: %s\n", ndx_strerror(ndx_errno()));
           return;
       }
       int count = call_get_player_count();
       call_on_player_join(42);
   }
   
   ndx_t* get_ndx_ptr(void) { return &ndx; }
   ```

4. **Host** - Define hook signatures:
   ```c
   // host.c
   #include <ttypt/ndx.h>
   NDX_DEF(int, get_player_count, void);   // from common/player_hooks.h
   NDX_DEF(void, on_player_join, int, player_id);
   
   int main(void) {
       ndx_load("./player_system.so");   // loads first (provides hooks)
       ndx_load("./game_logic.so");      // loads second (uses hooks)
       
       return 0;
   }
   ```

   **Note:** With the auto-init mechanism, modules automatically register their hooks when loaded.

#### What Happens at Runtime

When the host loads `game_logic.so`:

1. Host loads `game_logic.so` via `ndx_load()`
2. `game_logic.so`'s `ndx_install()` runs
3. `ndx_load("./player_system.so")` is called from within `ndx_install()`
4. If `player_system.so` not yet loaded: the library loads → `get_ndx_ptr()` → `ndx_install()`
5. After `ndx_load()` returns, `call_get_player_count()` works (hook already registered by host)

#### Checklist by Role

**Shared header author:**
- Use `NDX_DECL(type, name, args...)` for each hook
- Include `<ttypt/ndx.h>`

**Implementing module author:**
- Include shared header and `<ttypt/ndx.h>` and `src/papi.h`
- Declare `ndx_t ndx;` global
- Use `NDX_DEF(type, name, args...)` for each hook
- Implement the functions
- Provide `ndx_install()` and `get_ndx_ptr()`
- Link against `libndx`

**Dependent module author:**
- Include shared header and `<ttypt/ndx.h>` and `src/papi.h`
- Declare `ndx_t ndx;` global
- In `ndx_install()`: call `ndx_load()` for dependencies BEFORE using hooks
- Use `call_*()` macros to invoke hooks from dependencies
- Link against `libndx`

**Host author:**
- Include shared headers for all hooks your modules will need
- Use `NDX_DEF` for all hooks
- Link against `libndx`

## Linking

| Binary Type | Library | Reason |
|------------|---------|---------|
| Host | `libndx` | Provides ndx_load(), etc. |
| Module | `libndx` | Provides ndx API access |

**Minimal modules:** A module that only implements hooks (uses `NDX_DEF`) and doesn't call any ndx functions can link against just the libraries it needs (e.g., `-lnds`).

## Windows Notes
- Use `.dll` when calling `ndx_load()` (for example, `ndx_load("core.dll")`).

## Thread Safety

All public functions are mutex-protected. Multiple threads can safely:
- Load modules concurrently
- Call hooks concurrently
 - Register hooks concurrently

---

## Library Usage (Host-focused)

This section shows the common usage pattern for a host program that wants to expose hooks and load modules dynamically.

1) Define hook signatures

 - In your host source, define the hook signature with the provided macro. Example:

  ```c
  #include <ttypt/ndx.h>

  NDX_DEF(int, on_tick, int, dt);

  int main(void) {
      /* hooks auto-register on module load */
      ...
  }
  ```

 The `NDX_DEF` macro creates `on_tick_id` symbol and the typed helper `call_on_tick(dt)`. Hooks auto-register on module load.

2) Load modules

 - Use `ndx_load(path)` to load a module file. The loader will: load the library, wire the module's `ndx_t` by calling `get_ndx_ptr()` if present, run `ndx_install()` (first-time) or `ndx_open()` (reload).

 ```c
 if (ndx_load("./mods/mod_basic.so") != NDX_OK) {
     fprintf(stderr, "failed to load module: %s\n", ndx_strerror(ndx_errno()));
 }
 ```

3) Call hooks

 - Use the generated helper `call_<name>(...)` to call the hook. The macro packs arguments and dispatches to all modules that implement the named function.

 ```c
 int result = call_on_tick(16);
  (void) result; /* last-run hook return copied into result */
 ```

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
 cc -o mods/mod_basic.so mods/mod_basic.c -Iinclude -fPIC -shared -Llib -lndx
 ```

 - Run the host (ensure runtime linker finds `libndx.so`):
 ```sh
 LD_LIBRARY_PATH=./lib ./host
 ```

Troubleshooting (quick)

 - `call_*` reports `NDX_CALL BAD` / hook id is `NDX_INVALID`: ensure NDX_DEF was used to define the hook before calling ndx_load().
 - If `ndx_load()` fails to find symbols inside a module, check module exports and that module provides `get_ndx_ptr()`.

Examples

 - Minimal host (host.c):
 ```c
 #include <stdio.h>
 #include <ttypt/ndx.h>

 NDX_DEF(int, on_tick, int, dt);

 int main(void) {
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

 - Run the internal tests: `make test` (recommended).


## Documentation
- Header: `include/ttypt/ndx.h`
- Host impl: `src/libndx.c`
- Generate man pages: `make docs` then `man ndx`
