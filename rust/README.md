# ndx — Rust bindings for libndx

Ergonomic Rust bindings for [libndx](https://github.com/tty-pt/libndx), a C library for hook-based extensibility and dynamic module loading.

Modules (shared libraries) implement typed hooks; the host dispatches to all loaded modules that provide a given hook. Modules can load other modules, call other hooks, define new hooks, and manage isolated region namespaces.

---

## Crates

| Crate | Description |
|---|---|
| `ndx` | Ergonomic facade — FFI types, safe wrappers, macros, prelude. **This is what users add as a dependency.** |
| `ndx-macros` | Proc-macro crate (`#[ndx_listener]`, `#[ndx_hook_decl]`, `#[ndx_hook_def]`, etc.). Used automatically via `ndx`. |

---

## Prerequisites

The Rust crates link against `libndx`. Install it before building:

```sh
# See https://github.com/tty-pt/ci/blob/main/docs/install.md#install-ttypt-packages
# and use `libndx` as the package name.
```

`libndx.so` must be findable by the linker at build time and at runtime (`LD_LIBRARY_PATH` or installed to a standard lib path).

---

## Quick start

Add to your module's `Cargo.toml`:

```toml
[dependencies]
ndx = "0.1"

[lib]
crate-type = ["cdylib"]

[profile.dev]
panic = "abort"

[profile.release]
panic = "abort"
```

A minimal module (`src/lib.rs`):

```rust
use core::ffi::c_int;
use ndx::prelude::*;

// Emit `static mut NDX: NdxCtx` and `get_ndx_ptr` export.
ndx_module!();

// Implement the `on_tick` hook: receives dt, returns dt + 1.
#[ndx_listener]
pub fn on_tick(dt: c_int) -> c_int {
    dt + 1
}

// Module entry point — called once on first load.
ndx_install! {}
```

Build it as a shared library:

```sh
cargo build --release
# produces target/release/libmy_module.so
```

Load it from a C host (or another Rust binary using `ndx::load`):

```c
ndx_load("target/release/libmy_module");
int result = on_tick(10); // 11
```

---

## Calling another hook from within a module

Use `#[ndx_hook_decl]` to declare an outbound call. The macro generates a
per-TU static adapter (no `.init_array` registration) and an inline dispatch
function that routes through the injected `NDX` context.

```rust
use core::ffi::c_int;
use ndx::prelude::*;

ndx_module!();

// Declare on_tick as an outbound call — this module does NOT implement it.
#[ndx_hook_decl]
pub fn on_tick(dt: c_int) -> c_int {}

// rs_relay calls on_tick internally and doubles the result.
#[ndx_listener]
pub fn rs_relay(val: c_int) -> c_int {
    unsafe { on_tick(val) * 2 }
}

ndx_install! {}
```

---

## Defining a hook for other modules to implement

Use `#[ndx_hook_def]` to register the canonical adapter for a hook. Other
modules implement it with `#[ndx_listener]`. Dispatch routes through
`NDX.call` — any module can define hooks.

```rust
use core::ffi::c_int;
use ndx::prelude::*;

ndx_module!();

// Define the hook — this module owns the canonical adapter.
// Other modules implement it via #[ndx_listener].
#[ndx_hook_def]
pub fn my_event(val: c_int) -> c_int {}

// Call it from a listener in this same module.
#[ndx_listener]
pub fn trigger(val: c_int) -> c_int {
    unsafe { my_event(val) }
}

ndx_install! {}
```

---

## Per-region state

Modules can declare per-region state. The framework allocates one instance per
region the module is loaded into and injects it via `NDX.region_state` before
each hook dispatch.

```rust
use core::ffi::c_int;
use ndx::prelude::*;

ndx_module!();

ndx_region_state! {
    pub counter: c_int,
}
ndx_region_init!();

// Optional: called by the host on unload to free resources.
#[no_mangle]
pub extern "C" fn ndx_region_cleanup(_state: *mut core::ffi::c_void) {}

#[ndx_listener]
pub fn increment(dummy: c_int) -> c_int {
    let _ = dummy;
    unsafe {
        let s = NDX_RS!(NdxRegionState);
        (*s).counter += 1;
        (*s).counter
    }
}

ndx_install! {}
```

---

## Claiming a child region

```rust
ndx_module!();
ndx_claim!(2); // request a 2-bit child region

ndx_install! {
    // ndx_claim is evaluated by the parent's claim handler.
    // If approved, subsequent ndx_load / ndx_deny calls operate on
    // the child region.
}
```

---

## Safe wrappers

Inside a module, the following functions are available via `ndx::prelude::*`.
They all operate through the injected `NDX` context (set by the host before each
hook dispatch) and return `Result<(), NdxError>` or the relevant value.

| Function | Signature | Description |
|---|---|---|
| `load(path)` | `fn load(path: &str) -> Result<(), NdxError>` | Load a module by path (no `.so` extension) |
| `unload(path)` | `fn unload(path: &str) -> Result<(), NdxError>` | Unload a previously loaded module |
| `reload(path)` | `fn reload(path: &str) -> Result<(), NdxError>` | Reload a module in-place |
| `deny(hook, module)` | `fn deny(hook: &str, module: &str) -> Result<(), NdxError>` | Deny a hook for a specific module in the current region |
| `require_claim(bits, handler)` | `fn require_claim(bits: u8, handler: NdxClaimHandlerFn) -> Result<(), NdxError>` | Set a claim requirement and handler for child regions |
| `region_each(cb, data)` | `fn region_each(cb: NdxRegionEachCbFn, data: *mut c_void) -> Result<(), NdxError>` | Iterate over child regions |
| `with_region(region, cb, data)` | `fn with_region(region: u64, cb: NdxScopeFn, data: *mut c_void) -> Result<(), NdxError>` | Execute a closure in the context of a specific region |
| `current_region()` | `fn current_region() -> u64` | Return the current region ID |

Example — a module that loads a peer module and then operates in a child region:

```rust
use core::ffi::{c_int, c_void};
use ndx::prelude::*;

ndx_module!();

#[ndx_listener]
pub fn setup(_dummy: c_int) -> c_int {
    unsafe {
        // Load another module.
        load("path/to/peer_module").expect("peer load failed");

        // Deny a hook for a specific module in this region.
        deny("on_tick", "path/to/peer_module").ok();

        // Print the current region ID.
        let region = current_region();
        let _ = region;
    }
    0
}

ndx_install! {}
```

---

## Macro reference

| Macro / attribute | Where | What it does |
|---|---|---|
| `ndx_module!()` | crate root | Emits `static mut NDX: NdxCtx` and `get_ndx_ptr` export |
| `#[ndx_listener]` | module fn | Registers fn as a hook listener (adapter + `.init_array` entry) |
| `#[ndx_hook_decl]` | module fn | Declares an outbound hook call (per-TU adapter, no registration) |
| `#[ndx_hook_def]` | module fn | Defines canonical adapter + dispatch fn; routes through `NDX.call` |
| `ndx_install! { }` | crate root | Wraps body as `extern "C" fn ndx_install()` |
| `ndx_claim!(bits)` | crate root | Exports `ndx_claim: u8` symbol |
| `ndx_region_state! { }` | crate root | Declares `NdxRegionState` struct |
| `ndx_region_init!()` | after state | Exports `ndx_region_state_size()` |
| `NDX_RS!(Type)` | hook body | Typed pointer to current region state |

---

## Running the test suite

The tests require `libndx` built locally:

```sh
cd /path/to/libndx
make test
```

This builds both the C test harness and all Rust modules, then runs every test binary including `test_rust`.

---

## License

BSD-2-Clause — see [LICENSE](../LICENSE).
