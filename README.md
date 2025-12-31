# libndx
> A small C library for dynamic modding and hook-based extensibility.

## Installation
Check out [these instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md#install-ttypt-packages).
And use "libndx" as the package name.

## Architecture

libndx uses a **Caller/Callee** model. The Host defines the hooks, and Modules implement them.

* **Host**: Defines hooks and manages the lifecycle of modules via `ndx_load`.
* **Module**: Implements hooks and exports a state pointer (`ndx_t`) to the host.
* **Adapter**: Auto-generated typed wrappers (`call_<name>`) that handle the `void*` dispatching.

---

## 1. Define the Interface (Shared Header)

To allow multiple modules to talk to each other or the host, declare your hooks in a common header.

```c
// common/game_hooks.h
#include <ttypt/ndx.h>

// Use NDX_DECL to provide the signature and call_ helper to consumers
NDX_DECL(int, on_damage, int, player_id, int, damage);

```

## 2. Implement the Hook (Module)

The module must define the implementation and export its `ndx_t` state.

```c
// mods/combat_log.c
#include "common/game_hooks.h"

ndx_t ndx;

// Use NDX_DEF to implement the hook declared in the header
NDX_DEF(int, on_damage, int, player_id, int, damage) {
    printf("Player %d took %d damage\n", player_id, damage);
    return 0; 
}

// Required boilerplate for the loader
ndx_t* get_ndx_ptr(void) { return &ndx; }
void ndx_install(void)   { /* initialization */ }

```

## 3. Run the Hooks (Host)

The host defines the hook globally and triggers it after loading modules.

```c
// host.c
#include "common/game_hooks.h"

// The host must DEFINE the hook to allocate the internal ID
NDX_DEF(int, on_damage, int, player_id, int, damage);

int main() {
    ndx_load("./mods/combat_log.so");
    
    // Dispatches to all modules implementing 'on_damage'
    call_on_damage(1, 50); 
    
    return 0;
}

```

---

## API Quick Reference

### Macros

| Macro | Role | Description |
| --- | --- | --- |
| `NDX_DECL(...)` | **Interface** | Put in headers. Generates the `call_` wrapper. |
| `NDX_DEF(...)` | **Implementation** | Put in `.c` files. Registers the function to the hook system. |

### Host Functions

| Function | Description |
| --- | --- |
| `ndx_load(path)` | Loads/Reloads a module. Returns `NDX_OK` (0) or error. |
| `ndx_errno()` | Returns the last error code. |
| `ndx_strerror(err)` | Returns a human-readable error string. |
| `ndx_shutdown()` | Cleans up all loaded modules and resources. |

### Module Requirements

Every module **must** export:

1. `ndx_t* get_ndx_ptr(void)`: So the host can wire up the dispatch table.
2. `void ndx_install(void)`: Called once on the very first load.

---

## Building

**Host:**
`cc -o host host.c -lndx -pthread`

**Module:**
`cc -o mod.so mod.c -fPIC -shared`
