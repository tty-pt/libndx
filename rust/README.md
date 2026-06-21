# xy — Rust bindings for libxylem

Ergonomic Rust bindings for [libxylem](https://github.com/tty-pt/libxylem), a C library for hook-based extensibility and dynamic module loading.

Modules (shared libraries) implement typed hooks; the host dispatches to all loaded modules that provide a given hook. Modules can load other modules, call other hooks, define new hooks, and manage isolated region namespaces.

---

## Crates

| Crate | Description |
|---|---|
| `xy` | Ergonomic facade — FFI types, safe wrappers, macros, prelude. **This is what users add as a dependency.** |
| `xy-macros` | Proc-macro crate (`#[xy_impl]`, `#[xy_decl]`, `#[xy_def]`, etc.). Used automatically via `xy`. |

---

## Prerequisites

The Rust crates link against `libxylem`. Install it before building:

```sh
# See https://github.com/tty-pt/ci/blob/main/docs/install.md#install-ttypt-packages
# and use `libxylem` as the package name.
```

`libxylem.so` must be findable by the linker at build time and at runtime (`LD_LIBRARY_PATH` or installed to a standard lib path).

---

## Quick start

Add to your module's `Cargo.toml`:

```toml
[dependencies]
xy = "0.1"

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
use xy::prelude::*;

// Emit `static mut XY: XyCtx` and `get_xy_ptr` export.
xy_module!();

// Implement the `on_tick` hook: receives dt, returns dt + 1.
#[xy_impl]
pub fn on_tick(dt: c_int) -> c_int {
    dt + 1
}

// Module entry point — called once on first load.
xy_install! {}
```

Build it as a shared library:

```sh
cargo build --release
# produces target/release/libmy_module.so
```

Load it from a C host (or another Rust binary using `xy::load`):

```c
xy_load("target/release/libmy_module");
int result = on_tick(10); // 11
```

---

## Calling another hook from within a module

Use `#[xy_decl]` to declare an outbound call. The macro generates a
per-TU static adapter (no `.init_array` registration) and an inline dispatch
function that routes through the injected `XY` context.

```rust
use core::ffi::c_int;
use xy::prelude::*;

xy_module!();

// Declare on_tick as an outbound call — this module does NOT implement it.
#[xy_decl]
pub fn on_tick(dt: c_int) -> c_int {}

// rs_relay calls on_tick internally and doubles the result.
#[xy_impl]
pub fn rs_relay(val: c_int) -> c_int {
    unsafe { on_tick(val) * 2 }
}

xy_install! {}
```

---

## Defining a hook for other modules to implement

Use `#[xy_def]` to register the canonical adapter for a hook. Other
modules implement it with `#[xy_impl]`. Dispatch routes through
`XY.call` — any module can define hooks.

```rust
use core::ffi::c_int;
use xy::prelude::*;

xy_module!();

// Define the hook — this module owns the canonical adapter.
// Other modules implement it via #[xy_impl].
#[xy_def]
pub fn my_event(val: c_int) -> c_int {}

// Call it from a listener in this same module.
#[xy_impl]
pub fn trigger(val: c_int) -> c_int {
    unsafe { my_event(val) }
}

xy_install! {}
```

---

## Per-region state

Modules can declare per-region state. The framework allocates one instance per
region the module is loaded into and injects it via `XY.region_state` before
each hook dispatch.

```rust
use core::ffi::c_int;
use xy::prelude::*;

xy_module!();

xy_region_state! {
    pub counter: c_int,
}
xy_region_init!();

// Optional: called by the host on unload to free resources.
#[no_mangle]
pub extern "C" fn xy_region_cleanup(_state: *mut core::ffi::c_void) {}

#[xy_impl]
pub fn increment(dummy: c_int) -> c_int {
    let _ = dummy;
    unsafe {
        let s = XY_RS!(XyRegionState);
        (*s).counter += 1;
        (*s).counter
    }
}

xy_install! {}
```

---

## Claiming a child region

```rust
xy_module!();
xy_claim!(2); // request a 2-bit child region

xy_install! {
    // xy_claim is evaluated by the parent's claim handler.
    // If approved, subsequent xy_load / xy_deny calls operate on
    // the child region.
}
```

---

## Safe wrappers

Inside a module, the following functions are available via `xy::prelude::*`.
They all operate through the injected `XY` context (set by the host before each
hook dispatch) and return `Result<(), XyError>` or the relevant value.

| Function | Signature | Description |
|---|---|---|---|
| `load(xy, path)` | `unsafe fn load(xy: &XyCtx, path: &CStr) -> Result<(), XyError>` | Load a module by path (no `.so` extension) |
| `unload(xy, path)` | `unsafe fn unload(xy: &XyCtx, path: &CStr) -> Result<(), XyError>` | Unload a previously loaded module |
| `reload(xy, path)` | `unsafe fn reload(xy: &XyCtx, path: &CStr) -> Result<(), XyError>` | Reload a module in-place |
| `deny(xy, what, ty)` | `unsafe fn deny(xy: &XyCtx, what: &CStr, ty: XyDenyType) -> Result<(), XyError>` | Deny a hook or module in the current region |
| `require_claim(xy, handler, ud)` | `unsafe fn require_claim(xy: &XyCtx, handler: Option<XyClaimHandlerFn>, ud: *mut c_void) -> Result<(), XyError>` | Set a claim handler for child regions |
| `region_each(xy, f, ud)` | `unsafe fn region_each(xy: &XyCtx, f: Option<XyRegionEachCbFn>, ud: *mut c_void) -> Result<(), XyError>` | Iterate over child regions |
| `with_region(xy, region_id, f, ud)` | `unsafe fn with_region(xy: &XyCtx, region_id: u64, f: Option<XyScopeFn>, ud: *mut c_void) -> Result<(), XyError>` | Execute a closure in the context of a specific region |
| `current_region(xy)` | `unsafe fn current_region(xy: &XyCtx) -> u64` | Return the current region ID |

Example — a module that loads a peer module and then operates in a child region:

```rust
use core::ffi::{c_int, c_void};
use xy::prelude::*;

xy_module!();

#[xy_impl]
pub fn setup(_dummy: c_int) -> c_int {
    unsafe {
        // Load another module.  XY is injected by xy_module!().
        load(&XY, c"path/to/peer_module").expect("peer load failed");
    }
    0
}

xy_install! {}
```

---

## Macro reference

| Macro / attribute | Where | What it does |
|---|---|---|
| `xy_module!()` | crate root | Emits `static mut XY: XyCtx` and `get_xy_ptr` export |
| `#[xy_impl]` | module fn | Registers fn as a hook listener (adapter + `.init_array` entry) |
| `#[xy_decl]` | module fn | Declares an outbound hook call (per-TU adapter, no registration) |
| `#[xy_def]` | module fn | Defines canonical adapter + dispatch fn; routes through `XY.call` |
| `xy_install! { }` | crate root | Wraps body as `extern "C" fn xy_install()` |
| `xy_claim!(bits)` | crate root | Exports `xy_claim: u8` symbol |
| `xy_region_state! { }` | crate root | Declares `XyRegionState` struct |
| `xy_region_init!()` | after state | Exports `xy_region_state_size()` |
| `XY_RS!(Type)` | hook body | Typed pointer to current region state |

---

## Running the test suite

The tests require `libxylem` built locally:

```sh
cd /path/to/libxylem
make test
```

This builds both the C test harness and all Rust modules, then runs every test binary including `test_rust`.

---

## License

BSD-2-Clause — see [LICENSE](../LICENSE).
